# Changelog

## [Unreleased]

### Fixed
- `signaling_api.cpp::reconnect_loop()`: исправлен вызов `Poco::Timer::restart()` — убран второй аргумент. `restart()` принимает только один интервал (ms), второй вызов был ошибочным.

### Added
- `HttpTest` — новый класс в `cli/`, циклический тестер HTTP-эндпоинтов (`GET /health`, `GET /stat`, `POST /push`) через Poco HTTPClientSession
  - Автоматический запуск вместе с TestSender в `cli/main.cpp`
  - Вывод результата каждого запроса с телом ответа в консоль
  - Статистика `health_ok`, `stat_ok`, `push_ok`, `errors`
  - Сигнатурная обработка в `signal_handler` для корректной остановки по Ctrl+C
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

### Added
- `ReconnectStrategy` — новый класс в `lunaricorn/cpp/signaling_reconnect.h` для exponential backoff с full jitter (1s–60s)
- `SignalingConnector::set_auto_reconnect()` — включение автоматического переподключения при обрыве
- `SignalingConnector::reconnect_loop()` — цикл переподключения с exponential backoff
- `SubscriptionCache` — кеширование параметров подписки (`_sub_cache`) для автоматического восстановления после reconnect
- Автоматическое восстановление подписок при успешном переподключении

### Changed
- `signaling_api.h/cpp`: добавлены поля `_auto_reconnect`, `_reconnect_strategy`, `_sub_cache`; метод `reconnect_loop()`
- `signaling_api.cpp::start()`: сброс `_reconnect_strategy` при успешном ручном коннекте
- `signaling_api.cpp::stop()`: отключает `_auto_reconnect`, чтобы прервать reconnect loop
- `signaling_api.cpp::on_disconnect()`: при `_auto_reconnect == true` запускает `reconnect_loop()` вместо полной остановки
- `signaling_api.cpp::subscribe()`: кеширует параметры подписки для восстановления
- `signaling_api.cpp::unsubscribe()`: очищает кеш подписки
- `cli/main.cpp`: включён auto-reconnect через `connector.set_auto_reconnect(true)`

### Documentation
- `problems.md`: P1 помечен как ✅ Реализовано
- `TODO.md`: пункт "Добавить reconnect логику для клиентов" помечен как выполненный
- `problems.md`: полная актуализация — удалены решённые проблемы (HTTP endpoint, подписки, push), переписаны P1-P3, M1-M3, L1-L3 по текущему состоянию
- `TODO.md`: переписан в формат чеклиста готовности, исправлены статусы компонентов, удалены устаревшие данные
