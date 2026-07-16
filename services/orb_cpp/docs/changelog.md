# Changelog

Все заметные изменения в подпроекте orb_cpp.

Формат основан на [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- Создан подпроект `cli` — консольное приложение `orb_cli` для интеграционного тестирования сервиса через HTTP API:
  - `OrbHttpTest` — фоновый поток, циклически тестирующий все эндпоинты: GET /health, PUT/GET /blob/{id}, PUT/GET /meta/{id}.
  - `main.cpp` — точка входа с поддержкой аргументов командной строки (host, port), переменных окружения `ORB_HOST`/`ORB_PORT`, graceful shutdown, проверкой доступности сервера при старте.
  - `build.sh` — скрипт сборки (аналогичен signaling_cpp/cli/build.sh).
  - `CMakeLists.txt` — сборка с Boost.Beast, Boost.JSON, Poco (Net, Foundation, Util).

- Реализован Phase 1 сервиса orb_cpp:
  - `BlobStorage` — управление K/V таблицей `orb_blob` (key VARCHAR(128), value JSONB) через SOCI/PostgreSQL.
  - `MetaStorage` — управление таблицей `orb_meta` с колонками под `InternalMetaObject`.
  - `Engine` — класс-оркестратор, объединяющий BlobStorage и MetaStorage.
  - `HttpEndpoint` — HTTP API сервер на Boost.Beast с эндпоинтами:
    - `GET /health` — проверка состояния сервиса.
    - `GET/PUT /blob/{id}` — чтение/запись blob-объектов.
    - `GET/PUT /meta/{id}` — чтение/запись мета-объектов.
  - `InternalMetaObject` — структура метаданных (переименован из `OrbMeta`).
  - Graceful shutdown через сигналы SIGINT/SIGTERM (SignalWaiter).

### Changed

- `types.h`: структура `OrbMeta` переименована в `InternalMetaObject` для соответствия спецификации. Добавлен обратный alias `using OrbMeta = InternalMetaObject`.
- `main.cpp`: заглушка `make_engine` заменена на инициализацию `Engine` + `HttpEndpoint`. Убран неиспользуемый `LeaderConnector` в test-mode.
- `CMakeLists.txt`: добавлены новые исходники (`blob_storage.cpp`, `meta_storage.cpp`, `engine.cpp`, `http_endpoint.cpp`).

### Fixed

- Leader-коннектор теперь корректно прерывается по сигналу при ожидании.