# Changelog

Все значимые изменения этого подпроекта задокументированы в разделе `## [Unreleased]`.

## [Unreleased]

### Added

- RawEndpoint — TCP-сервер с acceptLoop + handleClients для бинарного протокола Signaling
- RE_Client — клиентское подключение с парсингом протокола, heartbeat, callbacks
- SignalingProto — serializeJson, deserializeJson, send_raw
- SignalingEngine — CRUD операций, система подписок и фильтрации событий
- MessageStorage — слой доступа к PostgreSQL через soci
- CLI клиент (`cli/`) — подключение, subscription, TestSender для автоматических тестов
- SignalWaiter + graceful shutdown (SIGTERM/SIGINT)
- Selftest через SignalingEngineTest
- Docker-файлы: Dockerfile, Dockerfile.base, Dockerfile.builder, Dockerfile.tester
- Design.md — полная документация архитектуры

### Fixed

- Deadlock в stop() — переработана синхронизация _clientsMutex (snapshot clients без блокировки)
- Non-blocking socket handling с errno EAGAIN/EWOULDBLOCK

### Changed

- Main switched from selftest to RawEndpoint as primary service