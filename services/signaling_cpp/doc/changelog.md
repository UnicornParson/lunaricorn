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

### Changed
- `main.cpp`: вызов `endpoint->connectEngine(engine)` после создания endpoint