import pika
import json
import sys
import time
from datetime import datetime
from pika.exceptions import AMQPConnectionError

# ===== CONFIG =====
RABBIT_HOST = "localhost"
QUEUE_NAME = "incoming_json"
OUTPUT_FILE = "/app/data/messages.log"

STATS_INTERVAL_SEC = 180
CONNECT_RETRIES = 300
CONNECT_DELAY_SEC = 1
# ==================


def connect_with_retry():
    last_error = None
    for i in range(CONNECT_RETRIES):
        print(f"try connect to Rabbit {i+1}/{CONNECT_RETRIES}")
        try:
            return pika.BlockingConnection(
                pika.ConnectionParameters(
                    host=RABBIT_HOST,
                    heartbeat=30,
                    blocked_connection_timeout=10,
                )
            )
        except AMQPConnectionError as e:
            last_error = e
            time.sleep(CONNECT_DELAY_SEC)

    raise RuntimeError("Failed to connect to RabbitMQ") from last_error


def main():
    connection = connect_with_retry()
    print("Rabbit ready")
    channel = connection.channel()

    channel.queue_declare(queue=QUEUE_NAME, durable=True)

    processed_since_last = 0
    last_stats_time = time.time()

    def print_stats_if_needed():
        nonlocal processed_since_last, last_stats_time

        now = time.time()
        if now - last_stats_time >= STATS_INTERVAL_SEC:
            q = channel.queue_declare(queue=QUEUE_NAME, passive=True)
            queue_size = q.method.message_count

            ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            print(
                f"[{ts}] "
                f"processed={processed_since_last}, "
                f"queue_size={queue_size}"
            )

            processed_since_last = 0
            last_stats_time = now

    def callback(ch, method, properties, body):
        nonlocal processed_since_last

        try:
            raw = body.decode("utf-8")
            data = json.loads(raw)

            # строгая проверка формата
            for key in ("o", "t", "m", "dt", "type"):
                if key not in data:
                    raise ValueError(f"missing field '{key}'")

            owner = data["o"]
            token = data["t"]
            message = data["m"]
            dt = data["dt"]
            msg_type = data["type"]

            line = f"{dt} [{msg_type}] {owner}::{token} {message}"

            with open(OUTPUT_FILE, "a", encoding="utf-8") as f:
                f.write(line + "\n")
                f.flush()

            ch.basic_ack(delivery_tag=method.delivery_tag)

            processed_since_last += 1
            print_stats_if_needed()

        except Exception as e:
            # формат неверный — печатаем в консоль
            try:
                bad_msg = body.decode("utf-8")
            except Exception:
                bad_msg = repr(body)

            print(f"INVALID MESSAGE: {e}")
            print(bad_msg)
            # ack не отправляем

    channel.basic_qos(prefetch_count=1)
    channel.basic_consume(
        queue=QUEUE_NAME,
        on_message_callback=callback,
        auto_ack=False
    )

    channel.start_consuming()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
