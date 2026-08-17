# High-Level Design: orb_cpp сервис

## Обзор

`orb_cpp` — C++ микросервис Key-Value хранилища с метаданными. Реализует HTTP API для управления blob-объектами (JSON) и их метаданными. Также включает клиентскую библиотеку `orb_client` для взаимодействия с сервером.

## Архитектура

```
┌──────────────────────────────────────────────────┐
│                   main.cpp                       │
│  ┌────────────┐  ┌──────────────┐  ┌───────────┐ │
│  │  Engine    │  │ HttpEndpoint │  │ Signal    │ │
│  │            │  │ (Boost.Beast)│  │ Waiter    │ │
│  └─────┬──────┘  └──────┬───────┘  └───────────┘ │
│        │                │                          │
│  ┌─────┴──────┐         │ HTTP :8081               │
│  │  Blob      │         │                          │
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

┌──────────────────────────────────────────────────┐
│                 orb_client                       │
│  ┌────────────┐  ┌──────────────┐                │
│  │ OrbClient  │  │ OrbObject    │                │
│  │ (IOrbCtrl) │  │ (lazy-load)  │                │
│  └────────────┘  └──────────────┘                │
│       ↕  HTTP + gzip                              │
│  ┌──────────────────────────────────────────┐    │
│  │              orb_cpp server              │    │
│  └──────────────────────────────────────────┘    │
└──────────────────────────────────────────────────┘
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
- **Интерфейс**: `contains`, `store`, `load`, `count`, `ok`, `search_by_tags`
- **Сериализация**: InternalMetaObject ↔ SQL через tags_to_pg_array/pg_array_to_tags
- **search_by_tags**: использует SQL `@>` оператор для поиска совпадений тегов в JSONB

### Engine
- **Файлы**: `engine.h`, `engine.cpp`
- Оркестратор, объединяющий BlobStorage и MetaStorage.
- Проверка состояния: `ok()` возвращает true только если оба хранилища работают.
- **Методы (Phase 1)**:
  - `store_blob(id, data)` — сохранить blob
  - `load_blob(id)` — загрузить blob
  - `store_meta(id, meta)` — сохранить метаданные
  - `load_meta(id)` — загрузить метаданные
- **Методы (Phase 2)**:
  - `generate_id()` — генерация UUID через Boost.UUID
  - `search_by_tags(tags)` — поиск по тегам через MetaStorage
  - `update_has_blob(id, value)` — авто-управление флагом has_content
  - `store_blob(id, data)` — расширен: сохраняет blob + обновляет has_content
  - `delete_blob(id)` — удаляет blob + сбрасывает has_content

### HttpEndpoint
- **Файлы**: `http_endpoint.h`, `http_endpoint.cpp`
- HTTP сервер на Boost.Beast (асинхронный, io_context).
- Прослушивает порт 8081 (настраивается через константы).
- **Session** — внутренний класс для обработки TCP-сессий.

#### Gzip-сжатие (Phase 2)
- `client_accepts_gzip(req)` — проверяет `Accept-Encoding: gzip` во входящем запросе
- `compress_gzip(data)` — сжимает данные через zlib (deflate с gzip header)
- `apply_gzip_if_needed(req, res)` — применяет сжатие если клиент поддерживает

#### Статистика API (Phase 2)
- `ApiStats` — структура с атомарными счётчиками:
  - `health_count`, `get_meta_count`, `put_meta_count`, `get_blob_count`, `put_blob_count`, `gen_id_count`, `search_count`
  - `bytes_in`, `bytes_out`
- `stats_timer_` — asio::steady_timer с интервалом 60 секунд
- По таймеру выводит сводку в `std::cout` через `MLOG_D` и сбрасывает счётчики

#### API эндпоинты

| Метод | Путь | Описание | Ответ |
|-------|------|----------|-------|
| GET | `/health` | Проверка состояния сервиса | `{"status":"ok"}` или `{"status":"unavailable"}` |
| GET | `/gen_id` | Генерация нового UUID | `{"id":"uuid-string"}` |
| GET | `/search?tags=tag1,tag2` | Поиск объектов по тегам | JSON-массив объектов |
| GET | `/blob/{id}` | Получить blob по ID | JSON-объект или 404 |
| PUT | `/blob/{id}` | Создать/обновить blob | `{"status":"stored"}` или 500 |
| GET | `/meta/{id}` | Получить метаданные по ID | JSON-объект или 404 |
| PUT | `/meta/{id}` | Создать/обновить метаданные | `{"status":"stored"}` или 500 |

#### Форматы данных

**PUT /blob/{id}**:
```json
{"key": "value", "nested": {"field": 42}}
```

**GET /blob/{id}**:
```json
{"key": "value", "nested": {"field": 42}}
```

**PUT /meta/{id}**:
```json
{
  "parent": "parent-id",
  "prev": "prev-id",
  "next": "next-id",
  "tags": ["tag1", "tag2"],
  "description": "описание"
}
```
- `has_content` игнорируется в запросе — управляется автоматически сервером.

**GET /meta/{id}**:
```json
{
  "id": "object-id",
  "parent": "parent-id",
  "prev": "prev-id",
  "next": "next-id",
  "tags": ["tag1", "tag2"],
  "description": "описание",
  "has_content": true
}
```

**GET /search?tags=tag1,tag2**:
```json
[
  {
    "id": "obj1",
    "tags": ["tag1", "tag2"],
    "has_content": false
  },
  {
    "id": "obj2",
    "tags": ["tag1"],
    "has_content": true
  }
]
```

### SignalWaiter
- **Файл**: `main.cpp` (класс определён там же)
- Обработка сигналов SIGINT/SIGTERM/SIGQUIT через sigwait().
- Обеспечивает graceful shutdown.

### IOrbController (клиентская часть)
- **Файлы**: `lunaricorn/cpp/orb_controller.h`
- Абстрактный интерфейс для клиентской коммуникации с orb_cpp сервером.
- Структура `OrbMetaData` — внутреннее представление мета-объекта:
  ```cpp
  struct OrbMetaData {
      std::string id;
      std::optional<std::string> parent;
      std::optional<std::string> prev;
      std::optional<std::string> next;
      std::vector<std::string> tags;
      std::string description;
      bool has_content = false;
  };
  ```
- Методы `IOrbController`:
  - `health()` — проверка доступности сервера
  - `get_meta(id)` — получить мета-объект
  - `put_meta(id, data)` — сохранить мета-объект
  - `get_blob(id)` — получить blob
  - `put_blob(id, data)` — сохранить blob
  - `search_by_tags(tags)` — поиск по тегам
  - `generate_id()` — генерация нового ID

### OrbClient (клиентская часть)
- **Файлы**: `lunaricorn/cpp/orb_client.h`, `lunaricorn/cpp/orb_client.cpp`
- HTTP-клиент, реализующий `IOrbController`.
- **Особенности**:
  - Поддерживает gzip-сжатие (отправляет `Accept-Encoding: gzip`, распаковывает gzip-ответы)
  - Периодический health check (таймер 10с)
  - Callback при изменении статуса сервера (alive ↔ dead)
  - Парсинг URL: `http://host:port/path` с поддержкой префикса пути (`target_prefix_`)
- **Конструктор**: `OrbClient(asio::io_context& ioc, const std::string& server_url)`
- **Управление health check**:
  - `start_health_check()` — запустить периодическую проверку
  - `stop_health_check()` — остановить
  - `set_status_callback(cb)` — установить callback при изменении статуса
  - `is_server_alive()` — проверить текущий статус
- **do_request(method, path, body)** — внутренний синхронный HTTP-запрос через Boost.Beast

### OrbObject (клиентская часть)
- **Файлы**: `lunaricorn/cpp/orb_object.h`, `lunaricorn/cpp/orb_object.cpp`
- Высокоуровневый объект, привязанный к `IOrbController&` и использующий его для работы с сервером.
- **Особенности**:
  - ID неизменяемый (устанавливается в конструкторе)
  - Lazy-loading: данные загружаются с сервера при первом обращении к любому полю (`ensure_loaded()`)
  - Кэширует: parent, prev, next, tags, description, has_content
- **Методы**:
  - `id()` — получить ID
  - `check()` — проверить существование на сервере (GET /meta/{id})
  - **Геттеры**: `parent()`, `prev()`, `next()`, `tags()`, `description()`, `has_content()` — lazy-load
  - **Сеттеры**: `set_parent()`, `set_prev()`, `set_next()`, `set_tags()`, `set_description()` — write to server + update cache
  - **Blob**: `store_blob(data)` — PUT /blob/{id}, `load_blob()` — GET /blob/{id}
  - **Навигация**: `follow_parent()`, `follow_prev()`, `follow_next()` — возвращают `std::optional<OrbObject>`

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

- `has_content` — автоматически управляется сервером:
  - Устанавливается в `true` при `store_blob(id, ...)`
  - Сбрасывается в `false` при `delete_blob(id)`
  - Игнорируется в клиентских запросах `PUT /meta/{id}`

## Конфигурация

Параметры подключения к БД загружаются из переменных окружения через `loadConfigFromEnvironment()` (библиотека `lunaricorn_api`):
- `DB_HOST`, `DB_PORT`, `DB_USER`, `DB_PASSWORD`, `DB_NAME`

## Запуск

Сборка и запуск через Docker:
```bash
./make_app.sh    # собрать контейнер
./it.sh          # запуск в интерактивном режиме
```

## Клиентская библиотека

### Использование OrbClient
```cpp
asio::io_context ioc;
OrbClient client(ioc, "http://127.0.0.1:8081");
client.start_health_check();

// Генерация ID
std::string id = client.generate_id();

// Работа с мета-объектами
OrbMetaData meta;
meta.id = id;
meta.description = "test";
client.put_meta(id, meta);

auto loaded = client.get_meta(id);

// Blob operations
json::object blob_data{{"key", json::value("value")}};
client.put_blob(id, blob_data);
auto loaded_blob = client.get_blob(id);

// Поиск
auto results = client.search_by_tags({"tag1", "tag2"});
```

### Использование OrbObject
```cpp
// Создание объекта
OrbObject obj(client, id);

// Установка полей (автоматически сохраняются на сервере)
obj.set_description("hello");
obj.set_tags({"tag1", "tag2"});

// Lazy-loading
std::string desc = obj.description();  // запросит с сервера

// Blob
obj.store_blob(blob_data);
auto blob = obj.load_blob();

// Навигация по ссылкам
auto parentObj = obj.follow_parent();  // std::optional<OrbObject>
```

## Сравнение с другими сервисами

| Аспект | signaling_cpp | orb_cpp | orb (Python) |
|--------|--------------|---------|-------------|
| Язык | C++ | C++ | Python |
| Протокол | Binary + HTTP | HTTP REST | gRPC + REST |
| Порт API | 8081 | 8081 | 8080 |
| DB | PostgreSQL | PostgreSQL | PostgreSQL |
| Leader | CLUSTER_LEADER_URL | ❌ Нет | CLUSTER_LEADER_URL |
| Maintenance | MAINTENANCE_* | ❌ Нет | MAINTENANCE_* |