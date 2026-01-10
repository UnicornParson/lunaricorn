import pika
import json
import sys
import time
from datetime import datetime
from pika.exceptions import AMQPConnectionError

RABBIT_HOST = "localhost"
QUEUE_NAME = "incoming_json"
OUTPUT_FILE = "/app/data/messages.log"

def connect_with_retry(retries=30, delay=1):
    last_error = None
    for attempt in range(1, retries + 1):
        try:
            rc =  pika.BlockingConnection(
                pika.ConnectionParameters(
                    host=RABBIT_HOST,
                    heartbeat=10,
                    blocked_connection_timeout=10,
                )
            )
            print("RabbitMQ ready")
            return rc
        except AMQPConnectionError as e:
            last_error = e
            print(f"RabbitMQ not ready (attempt {attempt}/{retries}), waiting...")
            time.sleep(delay)
    raise RuntimeError("Failed to connect to RabbitMQ") from last_error


def main():
    connection = connect_with_retry()
    print("### READY ###")
    channel = connection.channel()

    channel.queue_declare(
        queue=QUEUE_NAME,
        durable=True
    )

    def callback(ch, method, properties, body):
        try:
            message = body.decode("utf-8")
            print("on message " + message)
            json.loads(message)

            with open(OUTPUT_FILE, "a", encoding="utf-8") as f:
                f.write(message + "\n")
                f.flush()

            ch.basic_ack(delivery_tag=method.delivery_tag)

        except Exception as e:
            print(f"Error while processing message: {e}", file=sys.stderr)
            # не подтверждаем сообщение → оно останется в очереди

    channel.basic_qos(prefetch_count=1)
    channel.basic_consume(
        queue=QUEUE_NAME,
        on_message_callback=callback
    )

    print("Consumer started. Waiting for messages...")
    channel.start_consuming()


if __name__ == "__main__":
    main()
