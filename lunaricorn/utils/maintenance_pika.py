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

class MaintenanceClientMq:
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

    _connection: pika.BlockingConnection | None = None
    _channel: pika.channel.Channel | None = None
    _lock = threading.Lock()

    @staticmethod
    def _get_channel() -> pika.channel.Channel:
        with MaintenanceClientMq._lock:
            if (
                MaintenanceClientMq._connection
                and MaintenanceClientMq._connection.is_open
                and MaintenanceClientMq._channel
                and MaintenanceClientMq._channel.is_open
            ):
                return MaintenanceClientMq._channel

            # старое соединение прибьём аккуратно
            try:
                if MaintenanceClientMq._connection:
                    MaintenanceClientMq._connection.close()
            except Exception:
                pass

            MaintenanceClientMq._connection = pika.BlockingConnection(
                pika.ConnectionParameters(
                    host=MaintenanceClientMq.HOST,
                    port=MaintenanceClientMq.PORT,
                    heartbeat=MaintenanceClientMq.HEARTBEAT,
                    blocked_connection_timeout=MaintenanceClientMq.BLOCKED_TIMEOUT,
                )
            )

            channel = MaintenanceClientMq._connection.channel()
            channel.queue_declare(
                queue=MaintenanceClientMq.QUEUE_NAME,
                durable=True,
            )
            channel.confirm_delivery()

            MaintenanceClientMq._channel = channel
            return channel

    @staticmethod
    def wait_for_broker(timeout_sec: int = 60, poll_interval_sec: float = 1.0):
        deadline = time.time() + timeout_sec
        last_error = None

        while time.time() < deadline:
            try:
                connection = pika.BlockingConnection(
                    pika.ConnectionParameters(
                        host=MaintenanceClientMq.HOST,
                        port=MaintenanceClientMq.PORT,
                        heartbeat=MaintenanceClientMq.HEARTBEAT,
                        blocked_connection_timeout=MaintenanceClientMq.BLOCKED_TIMEOUT,
                    )
                )
                channel = connection.channel()

                channel.queue_declare(
                    queue=MaintenanceClientMq.QUEUE_NAME,
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
            channel = MaintenanceClientMq._get_channel()

            published = channel.basic_publish(
                exchange="",
                routing_key=MaintenanceClientMq.QUEUE_NAME,
                body=json.dumps(payload, ensure_ascii=False),
                properties=pika.BasicProperties(
                    delivery_mode=pika.DeliveryMode.Persistent
                ),
            )

            if not published:
                sys.stderr.write("Message was not confirmed by broker \n")

        except (AMQPConnectionError, ChannelClosedByBroker):
            # соединение считаем битым и сбрасываем
            with MaintenanceClientMq._lock:
                MaintenanceClientMq._channel = None
                MaintenanceClientMq._connection = None
            raise

        except Exception as e:
            sys.stderr.write(f"Failed to publish message: {e} \n")

    @staticmethod
    def push_log_message(owner: str, token: str, message: str):
        try:
            MaintenanceClientMq.push_maintenance_msg(
                type="log",
                owner=owner,
                token=token,
                message=message,
            )
        except Exception as e:
            sys.stderr.write(f"Error during log push: {e} \n")

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
            
            MaintenanceClientMq.push_log_message_http(
                owner=self.owner,
                token=self.token,
                message=msg,
            )
        except Exception as e:
            sys.stderr.write(f"log push failed: {e}\n")

