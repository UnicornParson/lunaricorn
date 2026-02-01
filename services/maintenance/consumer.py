import pika
import json
import sys
import time
import re
import os
from datetime import datetime
from pika.exceptions import AMQPConnectionError
import logging
import logging.config
# ===== CONFIG =========================================
RABBIT_HOST = "localhost"
QUEUE_NAME = "incoming_json"
DATA_DIR="/app/data"
OUTPUT_FILE = f"{DATA_DIR}/messages.log"
LOG_CONFIG = f"{DATA_DIR}/consumer_logging_config.json"  # Путь к файлу конфигурации

STATS_INTERVAL_SEC = 180
CONNECT_RETRIES = 300
CONNECT_DELAY_SEC = 1

LEVEL_RE = re.compile(r"\[(DEBUG|INFO|WARNING|ERROR|CRITICAL)\]")
PY_LOG_PREFIX_RE = re.compile(r"^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2},\d{3}\s+")
NON_MERGE_LEVELS = {"WARNING", "ERROR", "CRITICAL"}
MERGE_ROWS = bool(os.environ.get("MERGE_ROWS", "N") == "Y")

DEFAULT_LOG_CONFIG = """
{
    "version": 1,
    "disable_existing_loggers": false,
    "formatters": {
        "detailed": {
            "format": "%(asctime)s - %(name)s - %(levelname)s - %(message)s",
            "datefmt": "%Y-%m-%d %H:%M:%S"
        }
    },
    "handlers": {
        "console": {
            "class": "logging.StreamHandler",
            "level": "INFO",
            "formatter": "detailed",
            "stream": "ext://sys.stdout"
        },
        "file": {
            "class": "logging.handlers.RotatingFileHandler",
            "level": "INFO",
            "formatter": "detailed",
            "filename": null,
            "maxBytes": 104857600,
            "backupCount": 20,
            "encoding": "utf8"
        }
    },
    "loggers": {
        "": {
            "level": "INFO",
            "handlers": ["console", "file"]
        }
    }
}
"""


# ======================================================
# logging
def make_logging_config():
    with open(LOG_CONFIG, 'w', encoding='utf-8') as f:
        f.write(DEFAULT_LOG_CONFIG)

def setup_logging():
    # Создаем директорию для логов, если ее нет
    os.makedirs(DATA_DIR, exist_ok=True)
    
    # Создаем конфигурационный файл, если его нет
    if not os.path.exists(LOG_CONFIG):
        try:
            with open(LOG_CONFIG, 'w', encoding='utf-8') as f:
                f.write(DEFAULT_LOG_CONFIG)
            logger = logging.getLogger(__name__)
            logger.info(f"Создан файл конфигурации: {LOG_CONFIG}")
        except Exception as e:
            print(f"Ошибка при создании конфигурационного файла: {e}")
            # Создаем базовый логгер для вывода ошибки
            logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
            return logging.getLogger()
    
    try:
        # Читаем конфигурацию
        with open(LOG_CONFIG, 'r', encoding='utf-8') as f:
            config = json.load(f)
        
        # Устанавливаем путь к файлу лога
        log_file_path = f"{DATA_DIR}/consumer.log"
        config["handlers"]["file"]["filename"] = log_file_path
        
        # Применяем конфигурацию
        logging.config.dictConfig(config)
        
        # Проверяем, что файл создается
        logger = logging.getLogger(__name__)
        logger.info(f"Логирование настроено. Файл лога: {log_file_path}")
        
        return logger
    except Exception as e:
        print(f"Ошибка при настройке логирования: {e}")
        # Создаем базовый логгер для вывода ошибок
        logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
        return logging.getLogger()
logger = setup_logging()

# ======================================================

def rotate(log_file=OUTPUT_FILE):

    if not os.path.exists(log_file):
        print(f"Файл {log_file} не существует, создаём новый")
        open(log_file, 'a').close()
        return
    timestamp_ms = int(time.time() * 1000)

    file_dir = os.path.dirname(log_file)
    file_name = os.path.basename(log_file)
    name_parts = os.path.splitext(file_name)
    
    new_filename = f"{name_parts[0]}.{timestamp_ms}{name_parts[1]}"
    new_filepath = os.path.join(file_dir, new_filename)
    
    try:
        # Переименовываем файл
        os.rename(log_file, new_filepath)
        logger.info(f"Файл переименован: {log_file} -> {new_filepath}")
        
        # Создаём новый пустой файл
        open(log_file, 'a').close()
        logger.info(f"Создан новый файл: {log_file}")
        
    except Exception as e:
        logger.error(f"Ошибка при ротации файла: {e}")
        raise

def rotate_by_size(log_file=OUTPUT_FILE, max_size_mb=10):
    if not os.path.exists(log_file):
        return
    
    max_size_bytes = max_size_mb * 1024 * 1024
    file_size = os.path.getsize(log_file)
    
    if file_size > max_size_bytes:
        logger.info(f"Размер файла {file_size} байт превышает лимит {max_size_bytes} байт")
        rotate(log_file)
    else:
        logger.info(f"Ротация не требуется. Размер файла: {file_size} байт")

def connect_with_retry():
    last_error = None
    for i in range(CONNECT_RETRIES):
        logger.info(f"try connect to Rabbit {i+1}/{CONNECT_RETRIES}")
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
    logger.info("Rabbit ready")
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
            if level in NON_MERGE_LEVELS or not MERGE_ROWS:
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

            logger.error(f"INVALID MESSAGE: {e}")
            logger.error(bad_msg)

    channel.basic_qos(prefetch_count=1)
    channel.basic_consume(
        queue=QUEUE_NAME,
        on_message_callback=callback,
        auto_ack=False
    )

    channel.start_consuming()


if __name__ == "__main__":
    try:
        logger.info("Starting consumer...")
        rotate(log_file=OUTPUT_FILE)
        main()
    except KeyboardInterrupt:
        logger.info("Consumer interrupted by user.")
        flush_group()
        sys.exit(0)
