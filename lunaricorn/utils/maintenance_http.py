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
import requests
from .maintenance_utils import *
from .maintenance_http import *

class MaintenanceClient:
    HOST = None
    PORT = None
    QUEUE_NAME = "incoming_json"
    HTTP_BASE_URL = "http://localhost:8000"

    HEARTBEAT = 10
    BLOCKED_TIMEOUT = 10
    HTTP_TIMEOUT = 1.0
    HTTP_MAX_RETRIES = 3
    HTTP_RETRY_DELAY = 1.0
    HTTP_WAIT_POLL_INTERVAL = 1.0  # Default poll interval

    _lock = threading.Lock()

    _http_session: requests.Session | None = None
    _http_lock = threading.Lock()

    _http_inflight = 0
    _http_inflight_lock = threading.Lock()

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
    def _get_http_session() -> requests.Session:
        """Get or create a shared HTTP session with proper cleanup"""
        with MaintenanceClient._http_lock:
            if MaintenanceClient._http_session is None:
                MaintenanceClient._http_session = requests.Session()
                MaintenanceClient._http_session.headers.update(MaintenanceClient.http_headers)
            return MaintenanceClient._http_session

    @staticmethod
    def _close_http_session():
        """Close HTTP session to release resources"""
        with MaintenanceClient._http_lock:
            if MaintenanceClient._http_session:
                MaintenanceClient._http_session.close()
                MaintenanceClient._http_session = None

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
        session = MaintenanceClient._get_http_session()
        while time.time() < deadline:
            attempt += 1
            try:
                # Use the same session
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
        MaintenanceClient._close_http_session()
        # If we get here, we timed out
        raise RuntimeError(
            f"HTTP service at {MaintenanceClient.HTTP_BASE_URL} is not ready after {timeout_sec} seconds. "
            f"Last error: {last_error}"
        )

    @staticmethod
    def _make_http_request(url: str, payload: Dict[str, Any]) -> Dict[str, Any]:
        """Make HTTP request with retry logic (no persistent session)"""
        session = MaintenanceClient._get_http_session()
        try:
            response = session.post(
                url,
                json=payload,
                timeout=MaintenanceClient.HTTP_TIMEOUT,
            )
            response.raise_for_status()
            return response.json()

        except requests.exceptions.RequestException as e:
            sys.stderr.write(f"cannot send {payload} reason {e}")
            raise
        finally:
            MaintenanceClient._close_http_session()

    @staticmethod
    def push_maintenance_msg_http(type: str, owner: str, token: str, message: str):
        with MaintenanceClient._http_inflight_lock:
            payload = {
                "o": owner,
                "t": token,
                "m": message,
                "dt": datetime.now(timezone.utc).isoformat(),
                "type": type,
            }
            try:
                response = MaintenanceClient._make_http_request(
                    f"{MaintenanceClient.HTTP_BASE_URL}/log",
                    payload
                )
                return response
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
        session = MaintenanceClient._get_http_session()
        try:
            response = session.get(
                f"{MaintenanceClient.HTTP_BASE_URL}/health",
                timeout=MaintenanceClient.HTTP_TIMEOUT,
            )
            response.raise_for_status()
            return response.json()

        except requests.exceptions.RequestException as e:
            sys.stderr.write(f"HTTP health check failed: {e}\n")
            return None
        finally:
            MaintenanceClient._close_http_session()


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

