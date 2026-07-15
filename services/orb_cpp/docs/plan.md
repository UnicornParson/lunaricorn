# План реализации Phase 1 — orb_cpp сервис

## Обзор

Пошаговый план реализации сервиса orb_cpp согласно спецификации `spec_phase1.md`.  
Каждый пункт — отдельная задача (таска) для выполнения.

---

- [ ] **Таска 1: Переименовать `OrbMeta` в `InternalMetaObject` в `types.h`**
  - В спецификации используется имя `InternalMetaObject` для структуры мета-данных.
  - Текущая структура `OrbMeta` полностью соответствует описанию `InternalMetaObject`.
  - Добавить `using InternalMetaObject = OrbMeta;` либо переименовать напрямую.
  - Обновить комментарии в файле.

- [ ] **Таска 2: Создать `blob_storage.h` — объявление класса `BlobStorage`**
  - Интерфейс:
    - `bool contains(const std::string& id)`
    - `bool store(const std::string& id, const json::object& data)`
    - `std::optional<json::object> load(const std::string& id)`
    - `size_t count()`
    - `bool ok()`
  - Конструктор принимает `const DbConfig&`.
  - Приватные поля: `soci::session sql`, флаг `ok_`.
  - Использовать `#pragma once`, включить необходимые заголовки.

- [ ] **Таска 3: Реализовать `blob_storage.cpp` — имплементация `BlobStorage`**
  - Создание таблицы `orb_blob` (key VARCHAR(128) PRIMARY KEY, value JSONB NOT NULL).
  - `contains` — `SELECT 1 FROM orb_blob WHERE key = :id`.
  - `store` — `INSERT INTO orb_blob (key, value) VALUES (:id, :val::jsonb) ON CONFLICT (key) DO UPDATE SET value = :val::jsonb`.
  - `load` — `SELECT value FROM orb_blob WHERE key = :id`, парсинг JSON.
  - `count` — `SELECT COUNT(*) FROM orb_blob`.
  - `ok` — возвращает флаг, установленный при успешном connect/init.
  - Обработка ошибок через try/catch по аналогии с `message_storage.cpp`.

- [ ] **Таска 4: Создать `meta_storage.h` — объявление класса `MetaStorage`**
  - Интерфейс:
    - `bool contains(const std::string& id)`
    - `bool store(const std::string& id, const InternalMetaObject& data)`
    - `std::optional<InternalMetaObject> load(const std::string& id)`
    - `size_t count()`
    - `bool ok()`
  - Конструктор принимает `const DbConfig&`.
  - Поля: `soci::session sql`, `ok_`.
  - Вспомогательные приватные методы для сериализации `InternalMetaObject ↔ SQL`.

- [ ] **Таска 5: Реализовать `meta_storage.cpp` — имплементация `MetaStorage`**
  - Создание таблицы `orb_meta` с колонками под поля `InternalMetaObject`:
    - `id VARCHAR(128) PRIMARY KEY`,
    - `parent VARCHAR(128)`,
    - `prev VARCHAR(128)`,
    - `next VARCHAR(128)`,
    - `tags TEXT[]`,
    - `description TEXT`,
    - `has_content BOOLEAN NOT NULL DEFAULT FALSE`
  - `contains` — `SELECT 1 FROM orb_meta WHERE id = :id`.
  - `store` — upsert (INSERT ... ON CONFLICT DO UPDATE).
  - `load` — SELECT с маппингом колонок в поля структуры.
  - `count` — `SELECT COUNT(*) FROM orb_meta`.
  - `ok` — флаг состояния.
  - Использовать `tags_to_pg_array`/`pg_array_to_tags` из `message_storage.cpp` как образец.

- [ ] **Таска 6: Создать `engine.h` — объявление класса `Engine`**
  - Класс-оркестратор, объединяющий `BlobStorage` и `MetaStorage`.
  - Конструктор принимает `const DbConfig&`.
  - Предоставляет высокоуровневые методы:
    - `bool store_blob(const std::string& id, const json::object& data)`
    - `std::optional<json::object> load_blob(const std::string& id)`
    - `bool contains_blob(const std::string& id)`
    - `bool store_meta(const std::string& id, const InternalMetaObject& meta)`
    - `std::optional<InternalMetaObject> load_meta(const std::string& id)`
    - `bool contains_meta(const std::string& id)`
    - `bool ok()` — проверяет оба хранилища.
  - Приватные поля: `BlobStorage blobs_`, `MetaStorage metas_`.

- [ ] **Таска 7: Реализовать `engine.cpp` — имплементация `Engine`**
  - Проброс вызовов к соответствующим storage-классам.
  - Логирование операций (если необходимо).
  - Обработка ошибок.

- [ ] **Таска 8: Создать `http_endpoint.h` — объявление класса `HttpEndpoint`**
  - HTTP сервер на Boost.Beast.
  - Конструктор принимает порт, ссылку на `Engine`, и опционально хост.
  - Методы:
    - `void start()` — запуск listener'а.
    - `void stop()` — остановка.
  - Обработчики маршрутов (private):
    - `GET /blob/{id}` → load_blob
    - `PUT /blob/{id}` → store_blob (body: JSON)
    - `GET /meta/{id}` → load_meta
    - `PUT /meta/{id}` → store_meta
    - `GET /health` → проверка ok()

- [ ] **Таска 9: Реализовать `http_endpoint.cpp` — имплементация `HttpEndpoint`**
  - HTTP сервер с одним acceptor.
  - Разбор URI для извлечения id.
  - Сериализация/десериализация JSON для ответов.
  - Возврат HTTP 200/404/400/500.

- [ ] **Таска 10: Обновить `CMakeLists.txt`**
  - Добавить `blob_storage.cpp`, `meta_storage.cpp`, `engine.cpp`, `http_endpoint.cpp` в список исходников.
  - Убедиться, что все необходимые библиотеки (Boost.Beast, SOCI, pthread) подключены.

- [ ] **Таска 11: Обновить `main.cpp`**
  - Инициализация `DbConfig` из переменных окружения.
  - Создание `Engine`.
  - Создание и запуск `HttpEndpoint`.
  - Обработка сигналов (SIGINT/SIGTERM) для graceful shutdown.

- [ ] **Таска 12: Обновить документацию подпроекта**
  - `docs/changelog.md` — добавить запись о Phase 1.
  - `docs/hld.md` — описать архитектуру, компоненты, API эндпоинты, схему БД.
  - `docs/problems.md` — зафиксировать известные ограничения (если есть).

---

## Примечания

- `DbConfig` и `BrokenStorageError` — структуры из общей библиотеки `lunaricorn_api` (как в `message_storage.cpp`).
- `oid.h` уже предоставляет тип `oid = std::string` и функцию `make_oid()`.
- `types.h` уже содержит структуры `OrbMeta` и `OrbData`.
- Реализация storage-классов следует паттерну `message_storage.cpp` из `signaling_cpp`.
- HTTP-эндпоинты будут доступны после запуска сервиса для тестирования через `curl` или CLI-клиент (папка `cli/` будет создана позже).