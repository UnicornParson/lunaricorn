from contextlib import asynccontextmanager
from datetime import datetime
from typing import Optional
import json
import logging
import os
import threading
import time
from pydantic import BaseModel
from pydantic import field_validator
from pydantic.fields import FieldInfo
import uvicorn
import asyncio
from lunaricorn.utils.maintenance import *

from lunaricorn.utils.db_manager import DatabaseManager

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
DATA_DIR="/opt/app/data"
LOG_CONFIG = f"{DATA_DIR}/producer_logging_config.json"  # Путь к файлу конфигурации

class StorageHolder:
    s:Storage = None

# ======================================================
# logging
def make_logging_config():
    with open(LOG_CONFIG, 'w', encoding='utf-8') as f:
        f.write(DEFAULT_LOG_CONFIG)

def setup_logging():
    os.makedirs(DATA_DIR, exist_ok=True)
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
        log_file_path = f"{DATA_DIR}/log_storage.log"
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
MAX_RETRIES = 3
RETRY_DELAY = 1  # секунды
# ==================

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


def publish_message_safe(message: LogMessage):
    StorageHolder.s.push(message.o, message.t, message.m)
    return True

@asynccontextmanager
async def lifespan(app: FastAPI):
    """Application lifecycle handler"""
    # Startup - initialize connection
    logger.info("Starting FastAPI application...")
    
    if not StorageHolder.s:
        logger.error("StorageHolder is not initialized")
        raise RuntimeError
    
    yield
    
    # Shutdown - close connection
    logger.info("Shutting down FastAPI application...")
    del StorageHolder.s
    StorageHolder.s = None

app = FastAPI(
    title="Log Collector API",
    version="1.1.0",
    lifespan=lifespan  # Add lifecycle handler
)
app.mount("/static", StaticFiles(directory="static"), name="static")
app.add_middleware(GZipMiddleware, minimum_size=1000, compresslevel=9)

semaphore = asyncio.Semaphore(100)

@app.middleware("http")
async def concurrency_logger(request, call_next):
    async with semaphore:
        active = 100 - semaphore._value
        logging.info("Active requests: %d", active)
        return await call_next(request)

@app.get("/")
async def root():
    # TODO: embeded html view
    try:
        status = "connected"
    except Exception as e:
        status = f"disconnected: {str(e)}"
    
    return {
        "status": "online",
        "service": "Log Collector API",
        "rabbitmq": status
    }

@app.get("/health")
async def health_check():
    if StorageHolder.s != None:
        return {"status": "online"}
    raise HTTPException(
        status_code=500,
        detail=f"Internal server error"
    )

@app.post("/log")
async def receive_log(message: LogMessage, background_tasks: BackgroundTasks):
    try:
        background_tasks.add_task(publish_message_safe, message)
        logger.info(f"Received log from {message.o}::{message.t}, type: {message.type}")
        return { "status": "success" }
    except Exception as e:
        logger.error(f"Error processing log: {e}")
        raise HTTPException(
            status_code=500,
            detail=f"Internal server error: {str(e)}"
        )
    
@app.get("/log/pull")
async def pull_logs(offset: Optional[int] = Query(default=0, description="Starting offset for log retrieval")):
    try:
        logs = StorageHolder.s.pull(offset=offset)
        return {"logs": logs}
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Internal server error: {str(e)}")

@app.get("/log/pull-plain")
async def pull_logs_plain(offset: Optional[int] = Query(default=0, description="Starting offset for log retrieval")):

    try:
        logs = StorageHolder.s.pull(offset=offset)
        if not logs:
            content = "No logs available"
        else:
            formatted_logs = []
            for log in logs:
                timestamp = log.get('dt', 'N/A')
                message = log.get('msg', 'N/A')
                log_offset = log.get('offset', 'N/A')
                
                formatted_logs.append(f"{log_offset}[{timestamp}]: {message}")
            
            content = "\n".join(formatted_logs)

        headers={
            "Cache-Control": "no-cache, no-store, must-revalidate",
            "Pragma": "no-cache",
            "Expires": "0",
            "Vary": "Accept-Encoding",
        }
        return Response(
            content=content,
            headers=headers,
            media_type="text/plain"
        )
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Internal server error: {str(e)}")

@app.get("/log/download-plain")
async def download_logs_plain(offset: Optional[int] = Query(default=0, description="Starting offset for log retrieval")):
    try:
        logs = StorageHolder.s.pull(offset=offset)
        if not logs:
            content = "No logs available"
        else:
            formatted_logs = []
            for log in logs:
                timestamp = log.get('dt', 'N/A')
                message = log.get('msg', 'N/A')
                log_offset = log.get('offset', 'N/A')
                
                formatted_logs.append(f"{log_offset}[{timestamp}]: {message}")
            
            content = "\n".join(formatted_logs)

        timestamp_str = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
        filename = f"maintenance_{timestamp_str}.txt"
        response = PlainTextResponse(
            content=content,
            headers={
                "Content-Disposition": f"attachment; filename=\"{filename}\"",
                "Cache-Control": "no-cache, no-store, must-revalidate",
                "Pragma": "no-cache",
                "Expires": "0",
                "Vary": "Accept-Encoding",
            }
        )
        return response
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Internal server error: {str(e)}")
    
@app.post("/log/batch")
async def receive_log_batch(messages: list[LogMessage], background_tasks: BackgroundTasks):
    try:
        success_count = 0
        for message in messages:
            background_tasks.add_task(publish_message_safe, message)
            success_count += 1
        logger.info(f"Received {success_count} logs")
        return {
            "status": "success",
            "message": f"{success_count} logs accepted and queued",
            "count": success_count
        }
    except Exception as e:
        logger.error(f"Error processing batch logs: {e}")
        raise HTTPException(
            status_code=500,
            detail=f"Internal server error: {str(e)}"
        )


if __name__ == "__main__":
    config = DbConfig()

    env_mapping = {
        'db_type': ('db_type', "postgresql"),
        'db_host': ('db_host', None),
        'db_port': ('db_port', None),
        'db_user': ('db_user', None),
        'db_password': ('db_password', None),
        'db_dbname': ('db_name', None)
    }

    missing_vars = []

    for attr, (env_name, default) in env_mapping.items():
        value = os.getenv(env_name, default)
        if value is None:
            missing_vars.append(env_name)
        setattr(config, attr, value)

    if missing_vars:
        raise ValueError(
            f"Required environment variables are missing: {', '.join(missing_vars)}\n"
            f"Please add them to your Docker Compose or .env file:\n"
            f"  - {'\n  - '.join(missing_vars)}\n"
            f"See project README or documentation for details."
        )
    StorageHolder.s = Storage(config)
    uvicorn.run(
        app,
        host="0.0.0.0", 
        port=8000,
        workers=8
        )
