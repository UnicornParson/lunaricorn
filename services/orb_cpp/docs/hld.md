# High-Level Design: orb_cpp сервис

## Обзор

`orb_cpp` — C++ микросервис Key-Value хранилища с метаданными. 
Реализует HTTP API для управления blob-объектами (JSON) и их метаданными.

## Архитектура

```
┌──────────────────────────────────────────────────┐
│                   main.cpp                       │
│  ┌────────────┐  ┌──────────────┐  ┌───────────┐ │
│  │  Engine    │  │ HttpEndpoint │  │ Signal    │ │
│  │            │  │ (Boost.Beast)│  │ Waiter    │ │
│  └─────┬──────┘  └──────┬───────┘  └───────────┘ │
│        │                │                          │
│  ┌─────┴──────┐         │                          │
│  │  Blob      │         │ HTTP :8081               │
│  │  Storage   │         │                          │
│  └─────┬──────┘                                    │
│  ┌─────┴──────┐                                    │
│  │  Meta      │                                    │
│  │  Storage   │                                    │
│  └─────┬──────┘                                    │
└────────┼───────────────────────────────────────────┘
         │
         ▼
   ┌──────────┐
   │PostgreSQL│
   └──────────┘
```

## Компоненты

### BlobStorage
- **Файлы**: `blob_storage.h`, `blob_storage.cpp`
- **Таблица**: `orb_blob` (`key VARCHAR(128) PK`, `value JSONB NOT NULL`)
- **Интерфейс**: `contains`, `store`, `load`, `count`, `ok`
- **Библиотеки**: SOCI + PostgreSQL

### MetaStorage
- **Файлы**: `meta_storage.h`, `meta_storage.cpp`
- **Таблица**: `orb_meta` (id, parent, prev, next, tags, description, has_content)
- **Интерфейс**: `contains`, `store`, `load`, `count`, `ok`
- **Сериализация**: InternalMetaObject ↔ SQL через tags_to_pg_array/pg_array_to_tags

### Engine
- **Файлы**: `engine.h`, `engine.cpp`
- Оркестратор, объединяющий BlobStorage и MetaStorage.
- Проверка состояния: `ok()` возвращает true только если оба хранилища работают.

### HttpEndpoint
- **Файлы**: `http_endpoint.h`, `http_endpoint.cpp`
- HTTP сервер на Boost.Beast (асинхронный, io_context).
- Прослушивает порт 8081 (настраивается через константы).

### SignalWaiter
- **Файл**: `main.cpp` (класс определён там же)
- Обработка сигналов SIGINT/SIGTERM/SIGQUIT через sigwait().
- Обеспечивает graceful shutdown.

## API

| Метод | Путь           | Описание                  |
|-------|----------------|---------------------------|
| GET   | `/health`      | Проверка состояния сервиса|
| GET   | `/blob/{id}`   | Получить blob по ID       |
| PUT   | `/blob/{id}`   | Создать/обновить blob     |
| GET   | `/meta/{id}`   | Получить метаданные по ID |
| PUT   | `/meta/{id}`   | Создать/обновить метаданные|

### Форматы

**PUT /blob/{id}**:
```json
// Body: любой JSON-объект
{"key": "value", "nested": {"field": 42}}
```

**GET /blob/{id}**:
```json
// Response: сохранённый JSON-объект
{"key": "value", "nested": {"field": 42}}
```

**PUT /meta/{id}**:
```json
{
  "parent": "parent-id",
  "prev": "prev-id",
  "next": "next-id",
  "tags": ["tag1", "tag2"],
  "description": "описание",
  "has_content": true
}
```

**GET /health**:
```json
{"status": "ok"}
```

## Схема БД

```sql
CREATE TABLE IF NOT EXISTS orb_blob (
    key   VARCHAR(128) PRIMARY KEY,
    value JSONB NOT NULL
);

CREATE TABLE IF NOT EXISTS orb_meta (
    id          VARCHAR(128) PRIMARY KEY,
    parent      VARCHAR(128),
    prev        VARCHAR(128),
    next        VARCHAR(128),
    tags        TEXT[],
    description TEXT,
    has_content BOOLEAN NOT NULL DEFAULT FALSE
);
```

## Конфигурация

Параметры подключения к БД загружаются из переменных окружения через `loadConfigFromEnvironment()` (библиотека `lunaricorn_api`):
- `DB_HOST`, `DB_PORT`, `DB_USER`, `DB_PASSWORD`, `DB_NAME`

## Запуск

Сборка и запуск через Docker:
```bash
./make_app.sh    # собрать контейнер
./it.sh          # запуск в интерактивном режиме