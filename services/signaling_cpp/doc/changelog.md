# Changelog

## [Unreleased]

### Added
- `Telemetry` — глобальный класс-синглтон для сбора статистики сервиса
  - `recordPushSuccess()` / `recordError()` — учёт успешных push и ошибок
  - `setActiveClients()` — обновление количества активных клиентов
  - `pushesLastMinute()` / `errorsLastMinute()` — скользящее окно на 60 сек
  - `printReport()` — вывод статистики через MLOG_D раз в минуту
  - `toJson()` — экспорт метрик в boost::json::object
- Интеграция Telemetry в RawEndpoint: вызовы в acceptLoop, on_client_closed, processPushRequest
- Периодический вывод отчёта телеметрии в main.cpp (каждые 60 секунд)
- `parseSubscriptionPayload()` — парсинг JSON-фильтров подписки в RawEndpoint
- `sendEventToClient()` — отправка события подписанному клиенту
- `connectEngine()` — связывание RawEndpoint с SignalingEngine для callback-рассылки
- Полноценная обработка `processSubscription()`: парсинг фильтров + вызов `engine->subscribe()`
- Полноценная обработка `processPushRequest()`: создание StoredEventData, createEvent + dispatchEvent
- Автоматическая отписка клиента при disconnect (`on_client_closed` → `engine->unsubscribe`)

### Added
- `HttpServer` — HTTP сервер на Boost.Beast для REST API
  - `Session` — обработка HTTP подключений (async read/write)
  - Маршруты:
    - `GET /` — статус сервиса
    - `GET /health` — health check
    - `POST /push` — публикация события (аналог RawEndpoint::processPushRequest)
    - `GET /pull?offset=N` — запрос событий (аналог RawEndpoint::processQueryRequest)
    - `GET /stat` — JSON с текущей телеметрией (Telemetry + stats)
  - `HttpServerConfig` — конфигурация (address, port, num_threads)
  - `set_engine()` — связывание с SignalingEngine
- `StoredEventData` — структура для передачи событий через HTTP
- CMake: добавлен компонент `beast` в find_package и target_link_libraries

### Changed
- `main.cpp`: добавлена инициализация и запуск HttpServer alongside RawEndpoint
- `main.cpp`: порядок запуска/остановки: httpEndpoint → endpoint → stop order reverse
- CMakeLists.txt: `find_package(Boost REQUIRED COMPONENTS system filesystem json beast)`
