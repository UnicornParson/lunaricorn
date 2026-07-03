# Changelog

## [Unreleased]

### Added
- `parseSubscriptionPayload()` — парсинг JSON-фильтров подписки в RawEndpoint
- `sendEventToClient()` — отправка события подписанному клиенту
- `connectEngine()` — связывание RawEndpoint с SignalingEngine для callback-рассылки
- Полноценная обработка `processSubscription()`: парсинг фильтров + вызов `engine->subscribe()`
- Полноценная обработка `processPushRequest()`: создание StoredEventData, createEvent + dispatchEvent
- Автоматическая отписка клиента при disconnect (`on_client_closed` → `engine->unsubscribe`)

### Changed
- `main.cpp`: вызов `endpoint->connectEngine(engine)` после создания endpoint