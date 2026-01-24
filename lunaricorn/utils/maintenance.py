import json
from datetime import datetime, timezone
import pika
from typing import Optional, Dict, Any
from pika.exceptions import AMQPConnectionError, ChannelClosedByBroker
import time
import os
import logging
import sys
import threading
import string
import requests

_BASE62_ALPHABET = string.digits + string.ascii_letters  # 0-9A-Za-z
def base62_encode(num: int) -> str:
    if num == 0:
        return _BASE62_ALPHABET[0]
    base = len(_BASE62_ALPHABET)
    chars = []
    while num:
        num, rem = divmod(num, base)
        chars.append(_BASE62_ALPHABET[rem])
    return ''.join(reversed(chars))


def apptoken() -> str:
    pid = os.getpid()
    ts_ns = time.time_ns()
    return f"t{base62_encode(pid)}_{base62_encode(ts_ns)}"

class MaintenanceClient:
    HOST = None
    PORT = None
    QUEUE_NAME = "incoming_json"
    HTTP_BASE_URL = "http://localhost:8000"

    HEARTBEAT = 10
    BLOCKED_TIMEOUT = 10
    HTTP_TIMEOUT = 10.0
    HTTP_MAX_RETRIES = 3
    HTTP_RETRY_DELAY = 1.0
    HTTP_WAIT_POLL_INTERVAL = 1.0  # Default poll interval

    _connection: pika.BlockingConnection | None = None
    _channel: pika.channel.Channel | None = None
    _lock = threading.Lock()
    _http_session: requests.Session | None = None
    _http_lock = threading.Lock()

    
    http_headers = {
        "Content-Type": "application/json",
        "Accept": "application/json",
        "Accept-Encoding": "gzip, deflate, br",
        "User-Agent": (
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:122.0) "
            "Gecko/20100101 Firefox/122.0"
        ),
        "Cache-Control": "no-store, no-cache, must-revalidate, max-age=0",
        "Pragma": "no-cache",
        "Expires": "0",
        "Connection": "close",
    }

    @staticmethod
    def _get_channel() -> pika.channel.Channel:
        with MaintenanceClient._lock:
            if (
                MaintenanceClient._connection
                and MaintenanceClient._connection.is_open
                and MaintenanceClient._channel
                and MaintenanceClient._channel.is_open
            ):
                return MaintenanceClient._channel

            # старое соединение прибьём аккуратно
            try:
                if MaintenanceClient._connection:
                    MaintenanceClient._connection.close()
            except Exception:
                pass

            MaintenanceClient._connection = pika.BlockingConnection(
                pika.ConnectionParameters(
                    host=MaintenanceClient.HOST,
                    port=MaintenanceClient.PORT,
                    heartbeat=MaintenanceClient.HEARTBEAT,
                    blocked_connection_timeout=MaintenanceClient.BLOCKED_TIMEOUT,
                )
            )

            channel = MaintenanceClient._connection.channel()
            channel.queue_declare(
                queue=MaintenanceClient.QUEUE_NAME,
                durable=True,
            )
            channel.confirm_delivery()

            MaintenanceClient._channel = channel
            return channel

    @staticmethod
    def wait_for_broker(timeout_sec: int = 60, poll_interval_sec: float = 1.0):
        deadline = time.time() + timeout_sec
        last_error = None

        while time.time() < deadline:
            try:
                connection = pika.BlockingConnection(
                    pika.ConnectionParameters(
                        host=MaintenanceClient.HOST,
                        port=MaintenanceClient.PORT,
                        heartbeat=MaintenanceClient.HEARTBEAT,
                        blocked_connection_timeout=MaintenanceClient.BLOCKED_TIMEOUT,
                    )
                )
                channel = connection.channel()

                channel.queue_declare(
                    queue=MaintenanceClient.QUEUE_NAME,
                    durable=True
                )

                connection.close()
                return

            except Exception as e:
                last_error = e
                time.sleep(poll_interval_sec)

        raise RuntimeError("RabbitMQ is not ready") from last_error

    @staticmethod
    def wait_for_http_service(timeout_sec: Optional[int] = None, 
                            poll_interval_sec: Optional[float] = None,
                            endpoint: str = "/health") -> bool:
        timeout_sec = timeout_sec or MaintenanceClient.HTTP_WAIT_TIMEOUT
        poll_interval_sec = poll_interval_sec or MaintenanceClient.HTTP_WAIT_POLL_INTERVAL
        
        deadline = time.time() + timeout_sec
        last_error = None
        attempt = 0
        
        sys.stderr.write(f"Waiting for HTTP service at {MaintenanceClient.HTTP_BASE_URL}{endpoint}...\n")
        
        while time.time() < deadline:
            attempt += 1
            try:
                # Create a temporary session for the check
                with requests.Session() as session:
                    response = session.get(
                        f"{MaintenanceClient.HTTP_BASE_URL}{endpoint}",
                        timeout=5.0  # Short timeout for health checks
                    )
                    
                    if response.status_code == 200:
                        sys.stderr.write(f"HTTP service is ready (attempt {attempt})\n")
                        return True
                    else:
                        sys.stderr.write(f"HTTP service returned status {response.status_code} (attempt {attempt})\n")
                        last_error = f"HTTP {response.status_code}"
                        
            except requests.exceptions.ConnectionError as e:
                sys.stderr.write(f"Connection failed (attempt {attempt}): {e}\n")
                last_error = str(e)
            except requests.exceptions.Timeout as e:
                sys.stderr.write(f"Timeout (attempt {attempt})\n")
                last_error = "timeout"
            except Exception as e:
                sys.stderr.write(f"Error (attempt {attempt}): {e}\n")
                last_error = str(e)
            
            # Check if we should continue
            if time.time() >= deadline:
                break
                
            # Wait before next attempt
            time.sleep(poll_interval_sec)
        
        # If we get here, we timed out
        raise RuntimeError(
            f"HTTP service at {MaintenanceClient.HTTP_BASE_URL} is not ready after {timeout_sec} seconds. "
            f"Last error: {last_error}"
        )

    @staticmethod
    def push_maintenance_msg(type: str, owner: str, token: str, message: str):
        payload = {
            "o": owner,
            "t": token,
            "m": message,
            "dt": datetime.now(timezone.utc).isoformat(),
            "type": type,
        }

        try:
            channel = MaintenanceClient._get_channel()

            published = channel.basic_publish(
                exchange="",
                routing_key=MaintenanceClient.QUEUE_NAME,
                body=json.dumps(payload, ensure_ascii=False),
                properties=pika.BasicProperties(
                    delivery_mode=pika.DeliveryMode.Persistent
                ),
            )

            if not published:
                sys.stderr.write("Message was not confirmed by broker \n")

        except (AMQPConnectionError, ChannelClosedByBroker):
            # соединение считаем битым и сбрасываем
            with MaintenanceClient._lock:
                MaintenanceClient._channel = None
                MaintenanceClient._connection = None
            raise

        except Exception as e:
            sys.stderr.write(f"Failed to publish message: {e} \n")

    @staticmethod
    def _make_http_request(url: str, payload: Dict[str, Any]) -> Dict[str, Any]:
        """Make HTTP request with retry logic (no persistent session)"""
        last_exception = None


        for attempt in range(MaintenanceClient.HTTP_MAX_RETRIES):
            try:
                with requests.post(
                    url,
                    json=payload,
                    headers=MaintenanceClient.http_headers,
                    timeout=MaintenanceClient.HTTP_TIMEOUT,
                ) as response:
                    response.raise_for_status()
                    return response.json()

            except requests.exceptions.RequestException as e:
                last_exception = e
                sys.stderr.write(
                    f"HTTP request failed (attempt {attempt + 1}/"
                    f"{MaintenanceClient.HTTP_MAX_RETRIES}): {e}\n"
                )

                if attempt < MaintenanceClient.HTTP_MAX_RETRIES - 1:
                    time.sleep(
                        MaintenanceClient.HTTP_RETRY_DELAY * (2 ** attempt)
                    )  # exponential backoff
                else:
                    sys.stderr.write(
                        f"All {MaintenanceClient.HTTP_MAX_RETRIES} HTTP attempts failed\n"
                    )
                    raise
        raise last_exception


    @staticmethod
    def push_maintenance_msg_http(type: str, owner: str, token: str, message: str):

        payload = {
            "o": owner,
            "t": token,
            "m": message,
            "dt": datetime.now(timezone.utc).isoformat(),
            "type": type,
        }
        try:
            # Send HTTP request to FastAPI endpoint
            response = MaintenanceClient._make_http_request(
                f"{MaintenanceClient.HTTP_BASE_URL}/log",
                payload
            )
            
            sys.stderr.write(f"HTTP message sent successfully: {owner}::{token} \n")
            return response
            
        except requests.exceptions.RequestException as e:
            # Re-raise for caller to handle
            raise
            
        except Exception as e:
            sys.stderr.write(f"Unexpected error during HTTP push: {e} \n")
            return None

    @staticmethod
    def push_log_message(owner: str, token: str, message: str):
        try:
            MaintenanceClient.push_maintenance_msg(
                type="log",
                owner=owner,
                token=token,
                message=message,
            )
        except Exception as e:
            sys.stderr.write(f"Error during log push: {e} \n")

    @staticmethod
    def push_log_message_http(owner: str, token: str, message: str) -> Optional[Dict[str, Any]]:
        try:
            return MaintenanceClient.push_maintenance_msg_http(
                type="log",
                owner=owner,
                token=token,
                message=message,
            )
        except Exception as e:
            sys.stderr.write(f"Error during HTTP log push: {e} \n")
            return None
        
    @staticmethod
    def http_health_check() -> Optional[Dict[str, Any]]:
        try:
            with requests.get(
                f"{MaintenanceClient.HTTP_BASE_URL}/health",
                headers=MaintenanceClient.http_headers,
                timeout=MaintenanceClient.HTTP_TIMEOUT,
            ) as response:
                response.raise_for_status()
                return response.json()

        except requests.exceptions.RequestException as e:
            sys.stderr.write(f"HTTP health check failed: {e}\n")
            return None


    @staticmethod
    def close_http_session():
        """Close HTTP session to release resources"""
        with MaintenanceClient._http_lock:
            if MaintenanceClient._http_session:
                MaintenanceClient._http_session.close()
                MaintenanceClient._http_session = None
class MaintenanceLogHandler(logging.Handler):
    _local = threading.local()
    def __init__(self, owner: str, token: str):
        super().__init__()
        self.owner = owner
        self.token = token

    def emit(self, record: logging.LogRecord):
        if getattr(self._local, "in_emit", False):
            return

        self._local.in_emit = True
        if record.name in ["__pika", "pika"]:
            print(f"pika {record}")
            return
        try:
            msg = self.format(record)
            
            MaintenanceClient.push_log_message_http(
                owner=self.owner,
                token=self.token,
                message=msg,
            )
        except Exception as e:
            sys.stderr.write(f"log push failed: {e}\n")

class MaintenanceLogHandlerMq(logging.Handler):
    _local = threading.local()
    def __init__(self, owner: str, token: str):
        super().__init__()
        self.owner = owner
        self.token = token

    def emit(self, record: logging.LogRecord):
        if getattr(self._local, "in_emit", False):
            return

        self._local.in_emit = True
        if record.name in ["__pika", "pika"]:
            print(f"pika {record}")
            return
        try:
            msg = self.format(record)
            
            MaintenanceClient.push_log_message_http(
                owner=self.owner,
                token=self.token,
                message=msg,
            )
        except Exception as e:
            sys.stderr.write(f"log push failed: {e}\n")

def setup_maintenance_logging(owner: str, token: str):
    host = os.getenv("MAINTENANCE_HOST")
    port = os.getenv("MAINTENANCE_PORT")

    if not host or not port:
        return

    try:
        port = int(port)
    except ValueError:
        return

    MaintenanceClient.HOST = host
    MaintenanceClient.PORT = port
    MaintenanceClient.HTTP_BASE_URL = f"http://{host}:{port}"
    # Ждём брокер
    wait_timeout = 60 * 5 # 5m
    MaintenanceClient.wait_for_http_service(timeout_sec=wait_timeout)

    handler = MaintenanceLogHandler(owner=owner, token=token)
    handler.setLevel(logging.INFO)

    formatter = logging.Formatter(
        "%(asctime)s [%(levelname)s] %(name)s: %(message)s"
    )
    handler.setFormatter(formatter)

    root_logger = logging.getLogger()
    root_logger.addHandler(handler)



