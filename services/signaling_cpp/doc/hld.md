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

## Внешние API

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

Содержит сервис для signaling_cpp (порт 8080).

---

*Создано: 03.07.2026*