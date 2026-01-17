import pika
import json
import sys
import time
import re
from datetime import datetime
from pika.exceptions import AMQPConnectionError

# ===== CONFIG =====
RABBIT_HOST = "localhost"
QUEUE_NAME = "incoming_json"
OUTPUT_FILE = "/app/data/messages.log"

STATS_INTERVAL_SEC = 180
CONNECT_RETRIES = 300
CONNECT_DELAY_SEC = 1

LEVEL_RE = re.compile(r"\[(DEBUG|INFO|WARNING|ERROR|CRITICAL)\]")
PY_LOG_PREFIX_RE = re.compile(r"^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2},\d{3}\s+")
NON_MERGE_LEVELS = {"WARNING", "ERROR", "CRITICAL"}
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
    current_group = {
        "owner": None,
        "message": None,
        "level": None,
        "count": 0,
        "line": None,
    }

    def flush_group():
        if current_group["count"] == 0:
            return

        line = current_group["line"]
        count = current_group["count"]

        if count > 1:
            # Берем оригинальную строку без ..repeated
            original_line = current_group["line"]
            line = f"{original_line} ..repeated {count} times."

        with open(OUTPUT_FILE, "a", encoding="utf-8") as f:
            f.write(line + "\n")
            f.flush()

        # Полностью сбрасываем группу
        current_group["owner"] = None
        current_group["message"] = None
        current_group["level"] = None
        current_group["count"] = 0
        current_group["line"] = None

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

            for key in ("o", "t", "m", "dt", "type"):
                if key not in data:
                    raise ValueError(f"missing field '{key}'")

            owner = data["o"]
            token = data["t"]
            message = data["m"]
            dt = data["dt"]
            msg_type = data["type"]
            normalized_message = PY_LOG_PREFIX_RE.sub("", message)
            # определяем уровень
            m = LEVEL_RE.search(message)
            level = m.group(1) if m else None

            line = f"{dt} [{msg_type}] {owner}::{token} {normalized_message}"

            # сообщения с высоким уровнем — всегда сразу
            if level in NON_MERGE_LEVELS:
                flush_group()
                with open(OUTPUT_FILE, "a", encoding="utf-8") as f:
                    f.write(line + "\n")
                    f.flush()
            else:
                # проверяем, можно ли склеить
                if (
                    current_group["count"] > 0
                    and current_group["owner"] == owner
                    and current_group["message"] == normalized_message
                    and current_group["level"] == level
                ):
                    current_group["count"] += 1
                    current_group["line"] = line
                else:
                    flush_group()
                    current_group.update(
                        owner=owner,
                        message=normalized_message,
                        level=level,
                        count=1,
                        line=line,
                    )

            ch.basic_ack(delivery_tag=method.delivery_tag)
            processed_since_last += 1
            print_stats_if_needed()
        except Exception as e:
            try:
                bad_msg = body.decode("utf-8")
            except Exception:
                bad_msg = repr(body)

            print(f"INVALID MESSAGE: {e}")
            print(bad_msg)

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
        flush_group()
        sys.exit(0)
