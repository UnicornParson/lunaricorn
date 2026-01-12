import json
from datetime import datetime, timezone
import pika
from pika.exceptions import AMQPConnectionError, ChannelClosedByBroker
import time
import os
import logging
import sys
import threading

def apptoken() -> str:
    pid = os.getpid()
    ts_ns = time.time_ns()
    return f"t{pid}_{float(ts_ns)}"


class MaintenanceClient:
    HOST = None
    PORT = None
    QUEUE_NAME = "incoming_json"

    HEARTBEAT = 30
    BLOCKED_TIMEOUT = 10

    # Устанавливаем логгер для pika, чтобы избежать рекурсии
    pika_logger = logging.getLogger("__pika")
    pika_logger.setLevel(logging.WARNING)  # Устанавливаем только предупреждения и ошибки

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
    def push_maintenance_msg(type: str, owner: str, token: str, message: str):
        payload = {
            "o": owner,
            "t": token,
            "m": message,
            "dt": datetime.now(timezone.utc).isoformat(),
            "type": type,
        }

        try:
            connection = pika.BlockingConnection(
                pika.ConnectionParameters(
                    host=MaintenanceClient.HOST,
                    port=MaintenanceClient.PORT,
                    heartbeat=MaintenanceClient.HEARTBEAT,
                    blocked_connection_timeout=MaintenanceClient.BLOCKED_TIMEOUT,
                )
            )
        except AMQPConnectionError as e:
            raise RuntimeError("Failed to connect to RabbitMQ") from e

        try:
            channel = connection.channel()

            channel.queue_declare(
                queue=MaintenanceClient.QUEUE_NAME,
                durable=True
            )

            channel.confirm_delivery()

            published = channel.basic_publish(
                exchange="",
                routing_key=MaintenanceClient.QUEUE_NAME,
                body=json.dumps(payload, ensure_ascii=False),
                properties=pika.BasicProperties(delivery_mode=pika.DeliveryMode.Persistent),
            )

            if not published:
                raise RuntimeError("Message was not confirmed by broker")

        except ChannelClosedByBroker as e:
            raise RuntimeError("Channel was closed by broker during publish") from e

        except Exception as e:
            # Здесь мы не хотим рекурсивного логирования, поэтому исключение будет обработано без вызова логгера.
            raise RuntimeError(f"Failed to publish message: {e}") from e

        finally:
            if connection and connection.is_open:
                connection.close()

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
            sys.stderr.write(f"Error during log push: {e}")


class MaintenanceLogHandler(logging.Handler):
    _local = threading.local()
    def __init__(self, owner: str, token: str):
        super().__init__()
        self.owner = owner
        self.token = token

    def emit(self, record: logging.LogRecord):
        if getattr(self._local, "in_emit", False):
            # Жёстко гасим рекурсию
            return

        self._local.in_emit = True
        if record.name in ["__pika", "pika"]:
            print(f"pika {record}")
            return
        try:
            msg = self.format(record)
            
            MaintenanceClient.push_log_message(
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

    # Ждём брокер
    MaintenanceClient.wait_for_broker()

    handler = MaintenanceLogHandler(owner=owner, token=token)
    handler.setLevel(logging.INFO)

    formatter = logging.Formatter(
        "%(asctime)s [%(levelname)s] %(name)s: %(message)s"
    )
    handler.setFormatter(formatter)

    root_logger = logging.getLogger()
    root_logger.addHandler(handler)


