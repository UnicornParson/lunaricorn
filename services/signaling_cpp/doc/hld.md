# HLD: High-Level Design — Signaling C++ Service

## Архитектура системы

```
┌──────────────────────────────────────────────────────────────────┐
│                        main.cpp                                   │
│                                                                   │
│  1. loadConfigFromEnvironment() → DbConfig                       │
│  2. SignalingEngine(dbcfg)                                       │
│  3. SignalingEngineTest(engine) → run() (selftest)               │
│  4. RawEndpoint(raw_host, raw_port, engine) → start()            │
│  5. SignalWaiter::wait() → graceful shutdown                     │
│  6. endpoint->stop()                                             │
│                                                                   │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│                     RawEndpoint                                   │
│  ┌─────────────────┐    ┌──────────────────┐                     │
│  │ acceptLoop()    │───▶│ handleClients()   │                     │
│  │ (Poco::Socket)  │    │ (poll + receive)  │                     │
│  └─────────────────┘    └────────┬─────────┘                     │
│                                  │                                │
│                    ┌─────────────┼─────────────┐                  │
│                    ▼             ▼             ▼                  │
│              RE_Client#1   RE_Client#2   RE_Client#N             │
│              (proto)       (proto)       (proto)                 │
│                    │             │             │                   │
│                    └─────────────┴─────────────┘                  │
│                                  │                                │
│                    ┌─────────────┼─────────────┐                  │
│                    ▼             ▼             ▼                  │
│              SignalingEngine                           │
│              ┌──────────────┐    ┌──────────────┐     │
│              │ subscribers_ │    │ on_event()   │     │
│              │ filter/match │───▶│ dispatch     │     │
│              └──────────────┘    └──────┬───────┘     │
│                                         │             │
│                              ┌──────────┴────────┐    │
│                              ▼                   ▼    │
│                       MessageStorage    StoredEvent  │
│                       ┌──────────┐                    │
│                       │ soci::sql│──▶ PostgreSQL      │
│                       └──────────┘                    │
└──────────────────────────────────────────────────────────────────┘
```

## Компоненты

### Telemetry

**Назначение:** Глобальный класс-синглтон для сбора и вывода статистики работы сервиса.

**Метрики:**
| Метрика | Тип | Описание |
|---------|-----|----------|
| `total_push_ok` | `uint64` | Общее количество успешных push через raw endpoint |
| `active_clients` | `size_t` | Текущее количество активных клиентов |
| `pushes_per_minute` | `uint64` | Количество успешных push за последнюю минуту (скользящее окно 60 с) |
| `errors_per_minute` | `uint64` | Количество ошибок протокола за последнюю минуту (скользящее окно 60 с) |

**Публичный API:**
| Метод | Назначение |
|-------|------------|
| `instance()` | Получение singleton-экземпляра |
| `recordPushSuccess()` | Увеличить счётчик успешных push и добавить метку в окно |
| `recordError()` | Добавить метку ошибки в скользящее окно |
| `setActiveClients(n)` | Установить текущее количество активных клиентов |
| `printReport()` | Вывод отчёта через `MLOG_D` |
| `toJson()` | Экспорт метрик в `boost::json::object` |

**Интеграция:**
- `RawEndpoint`: вызовы `recordPushSuccess()` / `recordError()` в `processPushRequest`, `setActiveClients()` в `acceptLoop` и `on_client_closed`
- `main.cpp`: периодический вызов `printReport()` каждые 60 секунд

**Принцип работы:**
- Потокобезопасность через `std::atomic` для total/active и `std::mutex` для скользящих окон
- Скользящее окно — `std::deque` с таймстемпами, evict старых записей при каждом чтении
- Не привязан к конкретному endpoint'у — может использоваться из любой части сервиса

### RawEndpoint

**Назначение:** TCP-сервер для бинарного протокола Signaling.

**Порты:**
- Raw: `8080` (по умолчанию)

**Поток:**
1. `acceptLoop()` — принятие входящих соединений через `Poco::ServerSocket`
2. `handleClients()` — полирование клиентов, чтение данных, dispatch
3. `send_hb()` — периодическая отправка heartbeat сервером (каждые 10 сек)

**Управление клиентами:**
- `_clients: map<uint64_t, RE_Client_ptr>` — карта активных клиентов
- `_nextId` — генерация уникальных ID
- `_clientsMutex` — синхронизация доступа

### RE_Client

**Назначение:** Представление одного клиентского подключения.

**Функционал:**
- Инкрементальный парсинг бинарного протокола (`IncomingPacketState`)
- Heartbeat таймеры (`client_hb`, `server_hb`)
- Отправка сообщений (`send_message`)
- Callbacks для сервера (`set_message_callback`)

### SignalingEngine

**Назначение:** Основной движер — CRUD операций + система подписок.

**Методы:**
| Метод | Назначение |
|-------|------------|
| `createEvent()` | Создать событие |
| `findEvents()` | Поиск событий с фильтрами |
| `findEventsByType()` | Поиск по типу |
| `getUniqueValues()` | Уникальные значения полей |
| `subscribe()` | Подписка клиента |
| `unsubscribe()` | Отписка клиента |
| `dispatchEvent()` | Рассылка подписчикам |

**Фильтрация:**
- `filter_types` — по типу события
- `filter_sources` — по источнику
- `filter_affected` — по affected JSON
- `filter_tags` — по тегам

### MessageStorage

**Назначение:** Слой доступа к PostgreSQL.

**Таблица:** `signaling_events`
| Поле | Тип | Описание |
|------|-----|----------|
| eid | BIGSERIAL | PRIMARY KEY |
| type | TEXT | event_type |
| payload | JSONB | данные |
| affected | JSONB | затронутые |
| ctime | TIMESTAMP | created_at |
| owner | TEXT | source |
| tags | TEXT[] | теги |

### CLI клиент

**Назначение:** Тестовый клиент для ручного и автоматического тестирования.

**Использование:**
```bash
cli/main.cpp <host> <port>
```

**Компоненты:**
- `SignalingConnector` — основное подключение (приём событий)
- `TestSender` — автоматическая отправка тестовых событий

### HttpServer

**Назначение:** HTTP сервер на Boost.Beast для REST API signaling сервиса.

**Порт:** `8081` (по умолчанию)

**Архитектура:**
```
HttpServer (Endpoint impl)
├── boost::asio::io_context (ioc_)
├── tcp::acceptor (acceptor_)
├── std::vector<std::thread> (threads_)
└── Session (per-connection)
    ├── boost::beast::flat_buffer (buffer_)
    ├── http::request (request_)
    ├── tcp::socket (socket_)
    └── routing:
        ├── handle_root()       → GET /
        ├── handle_health()     → GET /health
        ├── handle_list()       → GET /v1/list/*
        ├── handle_push()       → POST /v1/push
        ├── handle_pull()       → GET /v1/pull
        ├── handle_clients()    → GET /v1/stat/clients
        └── handle_stat()       → GET /v1/stat
```

**Таблица маршрутизации:**

| Метод | Путь | Назначение | Хендлер |
|-------|------|------------|---------|
| GET | `/` | Статус сервиса | `handle_root()` |
| GET | `/health` | Health check | `handle_health()` |
| GET | `/v1/list/tags` | Уникальные теги | `handle_list("tags")` |
| GET | `/v1/list/types` | Уникальные типы событий | `handle_list("event_type")` |
| GET | `/v1/list/affected` | Уникальные affected значения | `handle_list("affected")` |
| GET | `/v1/list/owners` | Уникальные источники | `handle_list("source")` |
| GET | `/v1/stat/clients` | Статистика клиентов | `handle_clients()` |
| POST | `/v1/push` | Публикация события | `handle_push()` |
| GET | `/v1/pull?offset=N` | Запрос событий | `handle_pull()` |
| GET | `/v1/stat` | Телеметрия | `handle_stat()` |

**Конфигурация:**
| Параметр | Значение по умолчанию | Описание |
|----------|----------------------|----------|
| `address` | `"0.0.0.0"` | Адрес прослушки |
| `port` | `8081` | Порт |
| `num_threads` | `1` | Количество IO потоков |

**Публичный API:**
| Метод | Назначение |
|-------|------------|
| `HttpServerConfig()` | Конструктор конфигурации |
| `set_engine(engine)` | Связывание с SignalingEngine |
| `start()` | Запуск сервера (accept + threads) |
| `stop()` | Остановка сервера (close + join) |
| `io_context()` | Доступ к boost::asio::io_context |
| `get_telemetry_snapshot()` | Экспорт метрик |

### REST API

#### GET /

Возвращает статус сервиса.

**Response (200 OK):**
```json
{
  "status": "online",
  "service": "signaling"
}
```

#### GET /health

Health check endpoint.

**Response (200 OK):**
```json
{
  "status": "online"
}
```

#### GET /v1/list/{field}

Возвращает список уникальных значений указанного поля из хранилища.

**Query Parameters:**
| Параметр | Тип | Описание |
|----------|-----|----------|
| `field` | string | Имя поля: `tags`, `event_type`, `affected`, `source` |

**Response (200 OK):**
```json
{
  "field": "tags",
  "count": 3,
  "values": ["tag1", "tag2", "tag3"]
}
```

**Response (503 Service Unavailable):**
```json
{
  "error": "Engine not available"
}
```

#### GET /v1/stat/clients

Возвращает статистику активных клиентов и телеметрию.

**Response (200 OK):**
```json
{
  "telemetry": {
    "total_push_ok": 100,
    "active_clients": 5,
    "pushes_per_minute": 10,
    "errors_per_minute": 2
  },
  "stats": {
    "active_requests": 3
  }
}
```

#### POST /v1/push
Публикация события (аналог RawEndpoint::processPushRequest).

**Request Body (application/json):**
```json
{
  "type": "event_type_name",
  "source": "optional_source",
  "affected": ["item1", "item2"],
  "tags": ["tag1", "tag2"],
  "payload": { "key": "value" }
}
```

- `type` — обязательное поле (string)
- `source` — опциональное поле (string)
- `affected` — опциональное поле (array или string)
- `tags` — опциональное поле (array of strings)
- `payload` — опциональное, по умолчанию — весь объект минус `type`

**Response (200 OK):**
```json
{
  "status": "success",
  "event_id": 42,
  "published": true
}
```

**Response (400 Bad Request):**
```json
{
  "error": "Missing required field: type"
}
```

#### GET /v1/pull?offset=N
Запрос событий (аналог RawEndpoint::processQueryRequest).

**Query Parameters:**
| Параметр | Тип | Описание |
|----------|-----|----------|
| `offset` | int | Смещение (по умолчанию 0) |

**Response (200 OK):**
```json
{
  "events": [],
  "offset": 0,
  "count": 0
}
```

> **Примечание:** Ретrieve events через HTTP пока не реализован. В future — добавить метод `pullEvents()` в SignalingEngine.

#### GET /v1/stat
Возвращает JSON с текущей телеметрией.

**Response (200 OK):**
```json
{
  "telemetry": {
    "total_push_ok": 100,
    "active_clients": 5,
    "pushes_per_minute": 10,
    "errors_per_minute": 2
  },
  "stats": {
    "active_requests": 3,
    "endpoint_requests": 150,
    "endpoint_errors": 5
  }
}
```

### PostgreSQL

- Конфигурация через переменные окружения (`loadConfigFromEnvironment()`)
- Используется soci для доступа

### Log Collector

- `LogCollectorClient` (singleton) для отправки логов
- `MLog::is_stub = true` — stub-режим для production

## Протокол Signaling

### MessageHeader (fixed 24 bytes)

| Поле | Тип | Описание |
|------|-----|----------|
| magic | uint32 | `0x12345678` |
| version | uint8 | `1` |
| type | uint8 | MessageType |
| data_type | uint8 | ContentType |
| flags | uint8 | 0 |
| seq | uint64 | последовательность |
| data_len | uint32 | длина payload |
| crc | uint32 | CRC (если есть payload) |

### MessageType

| Значение | Тип |
|----------|-----|
| 0 | MT_Invalid |
| 1 | MT_HB (heartbeat) |
| 2 | MT_Response |
| 3 | MT_PubReq |
| 4 | MT_QueryReq |
| 5 | MT_Sub |

### ContentType

| Значение | Тип |
|----------|-----|
| 0 | CT_Raw |
| 1 | CT_Json |

### Формат данных

```
[MessageHeader (24 bytes)] [payload (data_len bytes, JSON if CT_Json)]
```

## Docker

### Образы

| Образ | Назначение |
|-------|------------|
| Dockerfile.base | Базовый образ |
| Dockerfile.builder | Образ для сборки |
| Dockerfile.tester | Образ для тестов |
| Dockerfile | Финальный образ |

### docker-compose.yaml

Сервис `signaling` собирается из `signaling_cpp/`:
- Внутренние порты: `8080` (raw), `8081` (HTTP)
- Внешние порты: `5555:8080` (raw), `5557:8081` (HTTP)
- Healthcheck: `GET /health` через HTTP (curl localhost:8081/health)
- `depends_on`: pg (healthy) + leader (healthy)
- Переменные окружения: `CLUSTER_LEADER_URL`, `db_*`, `MAINTENANCE_*`

---

*Создано: 03.07.2026*