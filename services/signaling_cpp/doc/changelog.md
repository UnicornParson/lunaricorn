# Changelog

## [Unreleased]

### Fixed
- `main.cpp`: SIGSEGV при отсутствии переменной окружения `CLUSTER_LEADER_URL` — `std::getenv()` возвращает `nullptr`, а прямое присваивание `std::string = nullptr` вызывает UB (вызов `strlen(nullptr)`). Добавлена проверка на `nullptr` до присваивания.

### Added
- `Session::handle_browse()` — реализация POST `/v1/browse` (совместимо с Python API BrowseRequest):
  - Парсинг JSON body: `event_types`, `sources`, `affected`, `tags`, `timestamp`, `limit`
  - Вызов `engine->findEvents()` с фильтрами
  - Возврат массива событий в формате: `{"events": [...], "count": N}`
  - Поддержка фильтрации по event_types (array), sources (array), affected (array), tags (array), timestamp (number), limit (number)
- `HttpTest::test_browse()` — тест POST `/v1/browse` в CLI:
  - Сначала отправляет тестовое событие через `POST /v1/push`
  - Затем запрашивает события через `POST /v1/browse` с фильтрами event_types и sources
  - Выводит результат вместе с push-ответом в одну строку
- Обновлён runner cycle `HttpTest::runner()` — добавлен phase 9 для test_browse (10 фаз вместо 9)
- LeaderConnector интеграция для signaling сервиса:
  - Регистрация в лидере через `register_service()` с node_type="signaling"
  - Автопинг (imalive) каждые 30 секунд через registration_timer_worker
  - Поддержка TEST_MODE=1 для тестового режима (отключение leader)
  - CLUSTER_LEADER_URL обязателен в обычном режиме
  - Ждущий цикл с сообщением раз в минуту (прерываемо через сигналы)
  - Graceful shutdown через `stop_registration_timer()`
- Конструктор `LeaderConnector(base_url, timeout)` в leader_api.cpp
- Обновлен it.sh для поддержки TEST_MODE
- Совместимое REST API для запроса уникальных значений и статистики:
  - `GET /` — статус сервиса (уже существовал)
  - `GET /health` — health check (уже существовал)
  - `GET /v1/list/tags` — список уникальных тегов
  - `GET /v1/list/types` — список уникальных типов событий
  - `GET /v1/list/affected` — список уникальных affected значений
  - `GET /v1/list/owners` — список уникальных источников (owners)
  - `GET /v1/stat/clients` — список активных клиентов
- `Session::handle_list()` — общий хендлер для `/v1/list/*` с вызовом `engine->getUniqueValues()`
- `Session::handle_clients()` — хендлер для `/v1/stat/clients` с телеметрией
- `HttpTest::test_root()` — тест GET `/` с выводом ✅/❌
- `HttpTest::test_list_tags()` — тест GET `/v1/list/tags`
- `HttpTest::test_list_types()` — тест GET `/v1/list/types`
- `HttpTest::test_list_affected()` — тест GET `/v1/list/affected`
- `HttpTest::test_list_owners()` — тест GET `/v1/list/owners`
- `HttpTest::test_clients()` — тест GET `/v1/stat/clients`
- `HttpTest::do_get()` — вспомогательный метод для generic GET запросов
- Статистика `root_ok`, `list_ok`, `clients_ok` в HttpTest
- `SignalingConnector::query()` — новый метод для отправки query-запросов (MT_QueryReq) с фильтрацией
- `TestSender::send_and_verify()` — после каждого push отправляется query на тот же тип события и проверяется, что событие найдено
  - Вывод `Query OK: found N events, matched type=...` при успехе
  - Вывод `Query FAIL` или `Query PARTIAL` с инкрементом error_count при проблемах
  - Раздельная обработка push-response (event_id) и query-response (events/count) в callback
- `RawEndpoint::processQueryRequest()`: полноценная обработка query-запросов через raw-протокол
  - Парсинг JSON-параметров: timestamp, types, sources, affected, tags, limit
  - Вызов `_engine->findEvents()` для поиска событий в БД
  - Формирование ответа с массивом найденных событий и их количеством
  - Валидация входных данных (пустой payload, не-JSON объект)
  - Логирование параметров запроса и количества результатов

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

### Changed
- HTTP API endpoints migrated to `/v1/` prefix for versioning and compatibility with Python signaling API:
  - `POST /push` → `POST /v1/push`
  - `GET /pull?offset=N` → `GET /v1/pull?offset=N`
  - `GET /stat` → `GET /v1/stat`
  - `GET /` и `GET /health` остались без изменений
- `HttpTest` client updated to use new `/v1/` prefixed endpoints
- Documentation updated to reflect new endpoint paths

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
