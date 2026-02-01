from fastapi import FastAPI, HTTPException, BackgroundTasks
from pydantic import BaseModel, field_validator
from pydantic.fields import FieldInfo
import pika
import json
from datetime import datetime
import logging
from contextlib import asynccontextmanager
from typing import Optional
import threading
import uvicorn
import os

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
DATA_DIR="/app/data"
LOG_CONFIG = f"{DATA_DIR}/producer_logging_config.json"  # Путь к файлу конфигурации


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
        log_file_path = f"{DATA_DIR}/producer.log"
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


# ===== CONFIG =====
RABBIT_HOST = "localhost"
QUEUE_NAME = "incoming_json"
RABBIT_USER = "guest"
RABBIT_PASSWORD = "guest"
MAX_RETRIES = 3
RETRY_DELAY = 1  # секунды
# ==================

# Global variables for connection
rabbit_connection = None
rabbit_channel = None
connection_lock = threading.Lock()

class LogMessage(BaseModel):
    """Log message model"""
    o: str  # owner
    t: str  # token
    m: str  # message
    type: str  # log type
    dt: Optional[str] = None  # datetime (optional, auto-generated)
    
    @field_validator('dt', mode='before')
    @classmethod
    def set_datetime(cls, v):
        """Set current time if dt not provided"""
        if v is None:
            return datetime.now().strftime("%Y-%m-%d %H:%M:%S,%f")[:-3]
        return v
    
    @field_validator('o', 't', 'm', 'type')
    @classmethod
    def validate_not_empty(cls, v: str, info: FieldInfo):
        """Validate that fields are not empty"""
        if not v or not v.strip():
            raise ValueError(f'{info.field_name} cannot be empty')
        return v.strip()


def init_rabbit_connection():
    """Initialize RabbitMQ connection on application startup"""
    global rabbit_connection, rabbit_channel
    
    try:
        credentials = pika.PlainCredentials(RABBIT_USER, RABBIT_PASSWORD)
        parameters = pika.ConnectionParameters(
            host=RABBIT_HOST,
            credentials=credentials,
            heartbeat=30,
            blocked_connection_timeout=10,
            connection_attempts=3,
            retry_delay=5
        )
        
        rabbit_connection = pika.BlockingConnection(parameters)
        rabbit_channel = rabbit_connection.channel()
        rabbit_channel.queue_declare(queue=QUEUE_NAME, durable=True)
        
        logger.info(f"Connected to RabbitMQ. Queue: {QUEUE_NAME}")
        return True
        
    except Exception as e:
        logger.error(f"Failed to initialize RabbitMQ connection: {e}")
        return False

def reconnect_rabbit():
    """Reconnect to RabbitMQ if connection is lost"""
    global rabbit_connection, rabbit_channel
    
    with connection_lock:
        # Close old connections if they exist
        try:
            if rabbit_channel and rabbit_channel.is_open:
                rabbit_channel.close()
            if rabbit_connection and rabbit_connection.is_open:
                rabbit_connection.close()
        except Exception:
            pass
        
        rabbit_connection = None
        rabbit_channel = None
        
        logger.warning("Attempting to reconnect to RabbitMQ...")
        return init_rabbit_connection()

def ensure_connection():
    """Verify and restore connection if needed"""
    global rabbit_connection, rabbit_channel
    
    if (rabbit_connection is None or rabbit_channel is None or 
        not rabbit_connection.is_open or not rabbit_channel.is_open):
        
        logger.warning("RabbitMQ connection lost, reconnecting...")
        if not reconnect_rabbit():
            raise RuntimeError("Failed to restore RabbitMQ connection")
    
    return True
def publish_message_safe(message: dict):
    """Безопасная отправка сообщения с обработкой ошибок Pika"""
    global rabbit_connection, rabbit_channel
    
    for attempt in range(MAX_RETRIES):
        try:
            with connection_lock:
                # Проверяем и восстанавливаем соединение
                if (rabbit_connection is None or rabbit_channel is None or 
                    not rabbit_connection.is_open or not rabbit_channel.is_open):
                    
                    # Закрываем старые соединения
                    try:
                        if rabbit_channel:
                            rabbit_channel.close()
                        if rabbit_connection:
                            rabbit_connection.close()
                    except:
                        pass
                    
                    # Создаем новое соединение
                    credentials = pika.PlainCredentials(RABBIT_USER, RABBIT_PASSWORD)
                    parameters = pika.ConnectionParameters(
                        host=RABBIT_HOST,
                        credentials=credentials,
                        heartbeat=30,
                        blocked_connection_timeout=10,
                        socket_timeout=5,
                        stack_timeout=5,
                        connection_attempts=3,
                        retry_delay=1
                    )
                    
                    rabbit_connection = pika.BlockingConnection(parameters)
                    rabbit_channel = rabbit_connection.channel()
                    rabbit_channel.queue_declare(queue=QUEUE_NAME, durable=True)
                    
                    logger.info(f"Reconnected to RabbitMQ. Queue: {QUEUE_NAME}")
                
                # Отправляем сообщение
                rabbit_channel.basic_publish(
                    exchange='',
                    routing_key=QUEUE_NAME,
                    body=json.dumps(message),
                    properties=pika.BasicProperties(
                        delivery_mode=2,
                        content_type='application/json',
                        timestamp=int(datetime.now().timestamp())
                    )
                )
                
                logger.debug(f"Message sent to queue {QUEUE_NAME}")
                return True
                
        except pika.exceptions.StreamLostError as e:
            logger.warning(f"Stream lost on attempt {attempt + 1}: {e}")
            if attempt < MAX_RETRIES - 1:
                time.sleep(RETRY_DELAY)
                continue
            else:
                logger.error(f"Failed to send message after {MAX_RETRIES} attempts")
                return False
                
        except Exception as e:
            logger.error(f"Unexpected error sending message: {e}")
            return False
    
    return False

@asynccontextmanager
async def lifespan(app: FastAPI):
    """Application lifecycle handler"""
    # Startup - initialize connection
    logger.info("Starting FastAPI application...")
    
    if not init_rabbit_connection():
        logger.error("Failed to initialize RabbitMQ connection")
        # Can continue and try to connect on first request
    
    yield
    
    # Shutdown - close connection
    logger.info("Shutting down FastAPI application...")
    try:
        if rabbit_channel and rabbit_channel.is_open:
            rabbit_channel.close()
        if rabbit_connection and rabbit_connection.is_open:
            rabbit_connection.close()
        logger.info("RabbitMQ connection closed")
    except Exception as e:
        logger.error(f"Error closing connection: {e}")

app = FastAPI(
    title="Log Collector API",
    version="2.0.0",
    lifespan=lifespan  # Add lifecycle handler
)

@app.get("/")
async def root():
    """Service health check endpoint"""
    try:
        ensure_connection()
        status = "connected"
    except Exception as e:
        status = f"disconnected: {str(e)}"
    
    return {
        "status": "online",
        "service": "Log Collector API",
        "rabbitmq": status,
        "queue": QUEUE_NAME
    }

@app.get("/health")
async def health_check():
    """Service health status endpoint"""
    try:
        # Check connection
        ensure_connection()
        
        # Check queue status
        queue_info = rabbit_channel.queue_declare(
            queue=QUEUE_NAME, 
            passive=True
        )
        
        return {
            "status": "healthy",
            "rabbitmq": "connected",
            "queue": {
                "name": QUEUE_NAME,
                "message_count": queue_info.method.message_count,
                "consumer_count": queue_info.method.consumer_count
            },
            "timestamp": datetime.now().isoformat()
        }
        
    except Exception as e:
        logger.error(f"Health check failed: {e}")
        raise HTTPException(
            status_code=503,
            detail=f"Service unavailable: {str(e)}"
        )

@app.post("/log")
async def receive_log(message: LogMessage, background_tasks: BackgroundTasks):
    """
    Receive log message and send it to RabbitMQ
    
    Message fields:
    - o (owner): log owner (required)
    - t (token): token/identifier (required)
    - m (message): message text (required)
    - type: log type (required)
    - dt: timestamp (optional, auto-generated)
    """
    try:
        # Convert Pydantic model to dict
        message_dict = message.dict()
        
        # Add message publishing as background task
        background_tasks.add_task(publish_message_safe, message_dict)
        
        logger.info(f"Received log from {message.o}::{message.t}, type: {message.type}")
        
        return {
            "status": "success",
            "message": "Log accepted and queued for processing",
            "data": {
                "owner": message.o,
                "token": message.t,
                "type": message.type,
                "timestamp": message.dt,
                "queue": QUEUE_NAME
            }
        }
        
    except Exception as e:
        logger.error(f"Error processing log: {e}")
        raise HTTPException(
            status_code=500,
            detail=f"Internal server error: {str(e)}"
        )

@app.post("/log/batch")
async def receive_log_batch(messages: list[LogMessage], background_tasks: BackgroundTasks):
    """
    Receive multiple log messages and send them to RabbitMQ
    """
    try:
        success_count = 0
        for message in messages:
            message_dict = message.dict()
            background_tasks.add_task(publish_message_safe, message_dict)
            success_count += 1
        
        logger.info(f"Received {success_count} logs")
        
        return {
            "status": "success",
            "message": f"{success_count} logs accepted and queued",
            "count": success_count,
            "queue": QUEUE_NAME
        }
        
    except Exception as e:
        logger.error(f"Error processing batch logs: {e}")
        raise HTTPException(
            status_code=500,
            detail=f"Internal server error: {str(e)}"
        )

@app.get("/rabbitmq/status")
async def rabbitmq_status():
    """Get RabbitMQ connection status"""
    try:
        ensure_connection()
        
        # Get additional connection info
        params = rabbit_connection.params
        is_open = rabbit_connection.is_open
        
        return {
            "status": "connected",
            "is_open": is_open,
            "host": params.host,
            "port": params.port,
            "virtual_host": params.virtual_host,
            "channel_open": rabbit_channel.is_open if rabbit_channel else False,
            "queue": QUEUE_NAME
        }
        
    except Exception as e:
        return {
            "status": "disconnected",
            "error": str(e),
            "is_open": rabbit_connection.is_open if rabbit_connection else False
        }

if __name__ == "__main__":

    uvicorn.run(app, host="0.0.0.0", port=8000)
