# --- Standard library ---
import json
import time
import os
import sys
import logging
import threading
import random
import string
import http.client
from datetime import datetime, timezone
from typing import Optional, List, Dict, Tuple, Any
import traceback
import requests
from requests.adapters import HTTPAdapter
from requests.exceptions import RequestException, ConnectionError
from urllib3.util.retry import Retry
import inspect
import asyncio
import os
import traceback
from typing import List, Dict, Optional, Tuple

import aiohttp
from aiohttp import ClientTimeout, ClientResponseError, ClientConnectorError


# --- Local project modules ---
from .maintenance_utils import *
from .maintenance_http import *

class MaintenanceClient_old:
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
        with MaintenanceClient_old._http_lock:
            if MaintenanceClient_old._http_session is None:
                MaintenanceClient_old._http_session = requests.Session()
                MaintenanceClient_old._http_session.headers.update(MaintenanceClient_old.http_headers)
            return MaintenanceClient_old._http_session

    @staticmethod
    def _close_http_session():
        """Close HTTP session to release resources"""
        with MaintenanceClient_old._http_lock:
            if MaintenanceClient_old._http_session:
                MaintenanceClient_old._http_session.close()
                MaintenanceClient_old._http_session = None

    @staticmethod
    def wait_for_http_service(timeout_sec: Optional[int] = None, 
                            poll_interval_sec: Optional[float] = None,
                            endpoint: str = "/health") -> bool:
        timeout_sec = timeout_sec or MaintenanceClient_old.HTTP_WAIT_TIMEOUT
        poll_interval_sec = poll_interval_sec or MaintenanceClient_old.HTTP_WAIT_POLL_INTERVAL
        
        deadline = time.time() + timeout_sec
        last_error = None
        attempt = 0
        
        sys.stderr.write(f"Waiting for HTTP service at {MaintenanceClient_old.HTTP_BASE_URL}{endpoint}...\n")
        session = MaintenanceClient_old._get_http_session()
        while time.time() < deadline:
            attempt += 1
            try:
                # Use the same session
                response = session.get(
                    f"{MaintenanceClient_old.HTTP_BASE_URL}{endpoint}",
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
        MaintenanceClient_old._close_http_session()
        # If we get here, we timed out
        raise RuntimeError(
            f"HTTP service at {MaintenanceClient_old.HTTP_BASE_URL} is not ready after {timeout_sec} seconds. "
            f"Last error: {last_error}"
        )

    @staticmethod
    def _make_http_request(url: str, payload: Dict[str, Any]) -> Dict[str, Any]:
        """Make HTTP request with retry logic (no persistent session)"""
        session = MaintenanceClient_old._get_http_session()
        try:
            response = session.post(
                url,
                json=payload,
                timeout=MaintenanceClient_old.HTTP_TIMEOUT,
            )
            response.raise_for_status()
            return response.json()

        except requests.exceptions.RequestException as e:
            sys.stderr.write(f"cannot send {payload} reason {e}")
            raise
        finally:
            MaintenanceClient_old._close_http_session()

    @staticmethod
    def push_maintenance_msg_http(type: str, owner: str, token: str, message: str):
        with MaintenanceClient_old._http_inflight_lock:
            payload = {
                "o": owner,
                "t": token,
                "m": message,
                "dt": datetime.now(timezone.utc).isoformat(),
                "type": type,
            }
            try:
                response = MaintenanceClient_old._make_http_request(
                    f"{MaintenanceClient_old.HTTP_BASE_URL}/log",
                    payload
                )
                return response
            except Exception as e:
                sys.stderr.write(f"Unexpected error during HTTP push: {e} \n")
                return None

    @staticmethod
    def push_log_message(owner: str, token: str, message: str):
        try:
            MaintenanceClient_old.push_maintenance_msg(
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
            return MaintenanceClient_old.push_maintenance_msg_http(
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
        session = MaintenanceClient_old._get_http_session()
        try:
            response = session.get(
                f"{MaintenanceClient_old.HTTP_BASE_URL}/health",
                timeout=MaintenanceClient_old.HTTP_TIMEOUT,
            )
            response.raise_for_status()
            return response.json()

        except requests.exceptions.RequestException as e:
            sys.stderr.write(f"HTTP health check failed: {e}\n")
            return None
        finally:
            MaintenanceClient_old._close_http_session()
'''
class LogCollectorClient:
    default = None
    i_lock = threading.Lock()

    @staticmethod
    def instance():
        with LogCollectorClient.i_lock:
            if not LogCollectorClient.default:
                host = os.getenv('MAINTENANCE_HOST')
                port_str = os.getenv('MAINTENANCE_PORT')

                if host is None:
                    raise ValueError("Переменная окружения MAINTENANCE_HOST не задана")
                if port_str is None:
                    raise ValueError("Переменная окружения MAINTENANCE_PORT не задана")

                port = int(port_str)
                base_url = f"http://{host}:{port}"
                LogCollectorClient.default = LogCollectorClient(base_url)
            return LogCollectorClient.default

    def __init__(self, base_url: str, pool_connections: int = 10, pool_maxsize: int = 20):
        self.base_url = base_url.rstrip('/')
        self.session = requests.Session()
        adapter = HTTPAdapter(
            pool_connections=pool_connections,
            pool_maxsize=pool_maxsize,
            max_retries=Retry(total=0)
        )
        self.session.headers.update({
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
        })
        self.session.mount('http://', HTTPAdapter(max_retries=1, pool_connections=10, pool_maxsize=10))
        self.session.mount('https://', HTTPAdapter(max_retries=1, pool_connections=10, pool_maxsize=10))
        self._lock = threading.Lock()  # push lock
        self.timeout = 10
        self.max_retries = 3
        self.retries_delay = 0.1
        self.reties_requested = 0


    def _url(self, path: str) -> str:
        return f"{self.base_url}{path}"

    def _handle(self, response: requests.Response):
        response.raise_for_status()
        if 'application/json' in response.headers.get('content-type', ''):
            return response.json()
        return response.text

    def get_status(self) -> dict:
        r = self.session.get(self._url('/'), timeout=self.timeout)
        return self._handle(r)

    def health_check(self) -> bool:
        try:
            r = self.session.get(self._url('/health'), timeout=self.timeout)
            r.raise_for_status()
            return True
        except requests.RequestException:
            return False

    def send_log(self, owner: str, token: str, message: str, log_type: str,
                 datetime: Optional[str] = None) -> dict:
        payload = {"o": owner, "t": token, "m": message, "type": log_type}
        if datetime:
            payload["dt"] = datetime
        
        last_exception = None
        for attempt in range(1, self.max_retries + 1):
            try:
                with self._lock:
                    r = self.session.post(self._url('/log'), json=payload, timeout=2)
                r.raise_for_status()
                return r.json()
            except (requests.ConnectionError, requests.Timeout, RequestException) as e:
                tb = e.__traceback__
                traceback.print_tb(tb)
                self.reties_requested += 1
                last_exception = e
                if attempt < self.max_retries:
                    time.sleep(self.retries_delay)
            except Exception as e:
                raise e
        if last_exception:
            raise last_exception

    def send_logs_batch(self, logs: List[dict]) -> dict:
        payload = []
        for log in logs:
            item = {"o": log["owner"], "t": log["token"], "m": log["message"], "type": log["type"]}
            if "datetime" in log:
                item["dt"] = log["datetime"]
            payload.append(item)
        
        last_exception = None
        for attempt in range(1, self.max_retries + 1):
            try:
                with self._lock:
                    r = self.session.post(self._url('/log/batch'), json=payload, timeout=2)
                r.raise_for_status()
                return r.json()
            except (requests.ConnectionError, requests.Timeout, RequestException) as e:
                tb = e.__traceback__
                traceback.print_tb(tb)
                self.reties_requested += 1
                last_exception = e
                if attempt < self.max_retries:
                    time.sleep(self.retries_delay)
            except Exception as e:
                raise e
        raise last_exception

    def pull_logs(self, offset: int = 0) -> List[dict]:
        r = self.session.get(self._url('/log/pull'), params={"offset": offset}, timeout=self.timeout)
        data = self._handle(r)
        return data.get("logs", [])

    def pull_logs_plain(self, offset: int = 0) -> str:
        r = self.session.get(self._url('/log/pull-plain'), params={"offset": offset}, timeout=self.timeout)
        r.raise_for_status()
        return r.text

    def download_logs_plain(self, offset: int = 0) -> Tuple[str, str]:
        r = self.session.get(self._url('/log/download-plain'), params={"offset": offset}, timeout=self.timeout)
        r.raise_for_status()
        cd = r.headers.get('content-disposition', '')
        filename = "logs.txt"
        if 'filename=' in cd:
            parts = cd.split('filename=')
            if len(parts) > 1:
                filename = parts[1].strip('"\'')
        return r.text, filename

    def close(self):
        """Закрыть сессию и освободить ресурсы."""
        self.session.close()
'''
class LogCollectorClient:
    _default = None
    _init_lock = asyncio.Lock()

    @classmethod
    async def instance(cls) -> "LogCollectorClient":
        """Асинхронный синглтон."""
        async with cls._init_lock:
            if cls._default is None:
                host = os.getenv('MAINTENANCE_HOST')
                port_str = os.getenv('MAINTENANCE_PORT')

                if host is None:
                    raise ValueError("Переменная окружения MAINTENANCE_HOST не задана")
                if port_str is None:
                    raise ValueError("Переменная окружения MAINTENANCE_PORT не задана")

                port = int(port_str)
                base_url = f"http://{host}:{port}"
                cls._default = LogCollectorClient(base_url)
                await cls._default._init_session()
            return cls._default

    def __init__(self, base_url: str, pool_connections: int = 10, pool_maxsize: int = 20):
        self.base_url = base_url.rstrip('/')
        self._pool_connections = pool_connections
        self._pool_maxsize = pool_maxsize
        self._session: Optional[aiohttp.ClientSession] = None
        self._request_lock = asyncio.Lock()          # аналог threading.Lock
        self.timeout = 10
        self.max_retries = 3
        self.retries_delay = 0.1
        self.retries_requested = 0

    async def _init_session(self):
        """Создаёт сессию aiohttp с нужными настройками."""
        connector = aiohttp.TCPConnector(
            limit=self._pool_maxsize,               # общее кол-во соединений
            limit_per_host=self._pool_connections,  # на один хост
            enable_cleanup_closed=True,
        )
        timeout = ClientTimeout(total=self.timeout)

        headers = {
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

        self._session = aiohttp.ClientSession(
            connector=connector,
            timeout=timeout,
            headers=headers
        )

    def _url(self, path: str) -> str:
        return f"{self.base_url}{path}"

    async def _handle_response(self, response: aiohttp.ClientResponse):
        """Обрабатывает ответ: проверяет статус, возвращает JSON или текст."""
        response.raise_for_status()
        content_type = response.headers.get('content-type', '')
        if 'application/json' in content_type:
            return await response.json()
        return await response.text()

    async def get_status(self) -> dict:
        """Получить статус сервера."""
        async with self._session.get(self._url('/')) as resp:
            return await self._handle_response(resp)

    async def health_check(self) -> bool:
        """Проверка здоровья сервера."""
        try:
            async with self._session.get(self._url('/health')) as resp:
                resp.raise_for_status()
                return True
        except (ClientResponseError, ClientConnectorError, asyncio.TimeoutError):
            return False

    async def send_log(self, owner: str, token: str, message: str, log_type: str,
                       datetime: Optional[str] = None) -> dict:
        """Отправить один лог (с повторными попытками)."""
        payload = {"o": owner, "t": token, "m": message, "type": log_type}
        if datetime:
            payload["dt"] = datetime

        last_exception = None
        for attempt in range(1, self.max_retries + 1):
            try:
                # Блокировка на уровне запроса, как в оригинале
                async with self._request_lock:
                    async with self._session.post(self._url('/log'), json=payload, timeout=2) as resp:
                        resp.raise_for_status()
                        return await resp.json()
            except (ClientConnectorError, asyncio.TimeoutError, ClientResponseError) as e:
                traceback.print_tb(e.__traceback__)
                self.retries_requested += 1
                last_exception = e
                if attempt < self.max_retries:
                    await asyncio.sleep(self.retries_delay)
            except Exception as e:
                raise e

        if last_exception:
            raise last_exception

    async def send_logs_batch(self, logs: List[Dict]) -> dict:
        """Отправить батч логов (с повторными попытками)."""
        payload = []
        for log in logs:
            item = {"o": log["owner"], "t": log["token"], "m": log["message"], "type": log["type"]}
            if "datetime" in log:
                item["dt"] = log["datetime"]
            payload.append(item)

        last_exception = None
        for attempt in range(1, self.max_retries + 1):
            try:
                async with self._request_lock:
                    async with self._session.post(self._url('/log/batch'), json=payload, timeout=2) as resp:
                        resp.raise_for_status()
                        return await resp.json()
            except (ClientConnectorError, asyncio.TimeoutError, ClientResponseError) as e:
                traceback.print_tb(e.__traceback__)
                self.retries_requested += 1
                last_exception = e
                if attempt < self.max_retries:
                    await asyncio.sleep(self.retries_delay)
            except Exception as e:
                raise e

        raise last_exception

    async def pull_logs(self, offset: int = 0) -> List[dict]:
        """Получить логи в виде списка словарей."""
        async with self._session.get(self._url('/log/pull'), params={"offset": offset}) as resp:
            data = await self._handle_response(resp)
            return data.get("logs", [])

    async def pull_logs_plain(self, offset: int = 0) -> str:
        """Получить логи в виде простого текста."""
        async with self._session.get(self._url('/log/pull-plain'), params={"offset": offset}) as resp:
            resp.raise_for_status()
            return await resp.text()

    async def download_logs_plain(self, offset: int = 0) -> Tuple[str, str]:
        """Скачать логи как текстовый файл. Возвращает (содержимое, имя_файла)."""
        async with self._session.get(self._url('/log/download-plain'), params={"offset": offset}) as resp:
            resp.raise_for_status()
            cd = resp.headers.get('content-disposition', '')
            filename = "logs.txt"
            if 'filename=' in cd:
                parts = cd.split('filename=')
                if len(parts) > 1:
                    filename = parts[1].strip('"\'')
            text = await resp.text()
            return text, filename

    async def close(self):
        """Закрыть сессию и освободить ресурсы."""
        if self._session and not self._session.closed:
            await self._session.close()

    async def __aenter__(self):
        if self._session is None:
            await self._init_session()
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        await self.close()

'''
class mlog_old:
    owner: str = ""
    token: str = ""
    @staticmethod
    def _get_caller_info() -> str:
        stack = inspect.stack()
        # stack[0] = _get_caller_info
        # stack[1] = log()
        # stack[2] = d/w/e or direct caller of log
        for frame_info in stack[2:]:
            func = frame_info.function
            # Skip any internal mlog methods
            if func in ('log', 'd', 'w', 'e'):
                continue
            filename = os.path.basename(frame_info.filename)
            lineno = frame_info.lineno
            return f"{filename}:{func}:{lineno}"
        return "unknown:unknown:0"
    @staticmethod
    def log(msg: str):
        caller_info = mlog._get_caller_info()
        full_msg = f"{caller_info} {msg}"
        client = await LogCollectorClient_async.instance()
        try:
            await client.send_log(mlog.owner, mlog.token, full_msg, "log")
        except Exception as e:
            print(f"mlogging error {e}")

    @staticmethod
    def d(msg: str):
        mlog.log(f"[ DEBUG ] {msg}")
    @staticmethod
    def w(msg: str):
        mlog.log(f"[WARNING] {msg}")
    @staticmethod
    def e(msg: str):
        mlog.log(f"[ ERROR ] {msg}")
'''


class mlog:
    owner: str = ""
    token: str = ""

    _loop: Optional[asyncio.AbstractEventLoop] = None
    _thread: Optional[threading.Thread] = None
    _client: Optional[LogCollectorClient] = None
    _init_lock = threading.Lock()

    @classmethod
    def _ensure_loop_and_client(cls):
        """Запускает цикл событий в фоновом потоке и инициализирует клиента (один раз)."""
        with cls._init_lock:
            if cls._loop is not None and not cls._loop.is_closed():
                return

            # Создаём и запускаем цикл в daemon-потоке
            cls._loop = asyncio.new_event_loop()
            cls._thread = threading.Thread(target=cls._run_loop, daemon=True)
            cls._thread.start()

            # Инициализируем клиента внутри этого цикла (синхронно ждём)
            future = asyncio.run_coroutine_threadsafe(cls._init_client(), cls._loop)
            future.result(timeout=10)  # ждём успешного создания клиента

    @classmethod
    def _run_loop(cls):
        asyncio.set_event_loop(cls._loop)
        cls._loop.run_forever()

    @classmethod
    async def _init_client(cls):
        cls._client = await LogCollectorClient.instance()

    @staticmethod
    def _get_caller_info() -> str:
        """Определяет, откуда вызван логгер (файл:функция:строка)."""
        stack = inspect.stack()
        for frame_info in stack[2:]:
            func = frame_info.function
            if func in ('log', 'd', 'w', 'e'):
                continue
            filename = os.path.basename(frame_info.filename)
            lineno = frame_info.lineno
            return f"{filename}:{func}:{lineno}"
        return "unknown:unknown:0"

    @classmethod
    def log(cls, msg: str):
        caller_info = cls._get_caller_info()
        full_msg = f"{caller_info} {msg}"

        # Гарантируем, что цикл и клиент готовы
        cls._ensure_loop_and_client()

        # Отправляем лог асинхронно, но ждём результат (синхронно)
        future = asyncio.run_coroutine_threadsafe(
            cls._client.send_log(cls.owner, cls.token, full_msg, "log"),
            cls._loop
        )
        try:
            future.result(timeout=5)   # таймаут на отправку одного лога
        except Exception as e:
            print(f"mlogging error: {e}")

    @classmethod
    def d(cls, msg: str):
        cls.log(f"[ DEBUG ] {msg}")

    @classmethod
    def w(cls, msg: str):
        cls.log(f"[WARNING] {msg}")

    @classmethod
    def e(cls, msg: str):
        cls.log(f"[ ERROR ] {msg}")

class MaintenanceLogHandler(logging.Handler):
    _local = threading.local()
    def __init__(self, owner: str, token: str):
        super().__init__()
        self.owner = owner
        self.token = token

    def emit(self, record: logging.LogRecord):
        if getattr(self._local, "in_emit", False):
            return
        # block
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

