# Журнал изменений проекта Lunaricorn

## 2026-06-30

### Исправлено

#### RawEndpoint::stop() — корректное завершение всех соединений
- **Проблема:** `stop()` устанавливал `_stopping = true`, закрывал серверный сокет,
  ждал завершения потоков через `join()`, и **только после этого** закрывал
  клиентские сокеты и очищал `_clients`. Это приводило к тому, что
  `handleClients()` мог бесконечно блокироваться на `poll()` или `receiveBytes()`,
  так как клиентские сокеты оставались открытыми.
- **Решение:** порядок действий в `stop()` изменён на корректный:
  1. Установка `_stopping = true`
  2. Закрытие серверного сокета (прерывает `acceptLoop()`)
  3. Закрытие **всех клиентских сокетов** (прерывает `poll()`/`receiveBytes()` в `handleClients()`)
  4. `join()` потоков `_acceptThread` и `_handlerThread`
  5. Очистка `_clients`
- Файл: `services/signaling_cpp/app/raw_endpoint.cpp`

#### RawEndpoint::stop() — исправление deadlock с `_clientsMutex`
- **Проблема:** `stop()` закрывал клиентские сокеты **удерживая `_clientsMutex`**.
  `handleClients()` после закрытия сокета получал исключение в `receiveBytes()`,
  вызывал `on_client_closed()`, которая также захватывает `_clientsMutex`.
  Так как `stop()` держал мьютекс и ждал `join()` потока, возникал deadlock.
- **Решение:** `stop()` теперь делает снапшот клиентов под мьютексом, закрывает
  сокеты **без удержания мьютекса**, затем join'ит потоки, и только потом
  очищает `_clients` под мьютексом.
- Файл: `services/signaling_cpp/app/raw_endpoint.cpp`

#### RawEndpoint::acceptLoop() — блокирующий acceptConnection() при остановке
- **Проблема:** `acceptLoop()` использовал блокирующий `acceptConnection()` без таймаута.
  При остановке поток не мог выйти из `acceptConnection()`, и `_acceptThread.join()`
  зависал навсегда.
- **Решение:** добавлен `poll()` с таймаутом 1 секунда перед `acceptConnection()`.
  Это позволяет `acceptLoop()` регулярно проверять флаг `_stopping`.
- Файл: `services/signaling_cpp/app/raw_endpoint.cpp`

#### RawEndpoint::send_hb() — deadlock с `_clientsMutex`
- **Проблема:** `send_hb()` захватывал `_clientsMutex`, затем вызывал `sendHeartbeat()`,
  которая также захватывает `_clientsMutex` — deadlock на том же потоке.
- **Решение:** `send_hb()` теперь собирает ID клиентов под мьютексом, а отправляет
  heartbeat'ы **без удержания мьютекса**.
- Файл: `services/signaling_cpp/app/raw_endpoint.cpp`

#### RawEndpoint::start() — защита от повторного запуска
- **Проблема:** `start()` проверял `_stopping == false` для пропуска запуска,
  но не проверял, что потоки уже запущены (`joinable()`).
- **Решение:** добавлена проверка `!_acceptThread.joinable() && !_handlerThread.joinable()`.

#### PROBLEM-015: Двойной буфер в SignalingConnector::send_message() — критический баг протокола
- **Проблема:** `send_message()` при отправке heartbeat (пустые данные) делал
  `buf.resize(sizeof(MessageHeader))`, создавая 24 нулевых байта. Затем
  `serializeJson()` добавлял ещё 24 корректных байта через `buf.insert()`.
  Итоговый пакет на wire: 48 байт, первые 24 — нули, magic=0x00000000.
  Сервер обрывал соединение: `invalid header magic: expected=0x12345678 actual=0x00000000`.
- **Решение:** убран `buf.resize(sizeof(MessageHeader))`. Для пустых данных
  буфер остаётся пустым, `serializeJson()` сам добавляет только 24 корректных байта.
- **Файл:** `lunaricorn/cpp/signaling_api.cpp :: send_message()`

#### PROBLEM-015: Receive-буфер 26 байт — фрагментация пакетов
- **Проблема:** `kRecvBufSize = sizeof(MessageHeader) + 2 = 26` байт. Ответы
  сервера с JSON-данными (100-200+ байт) обрезались.
- **Решение:** размер увеличен до 65536 байт (64KB).
- **Файл:** `lunaricorn/cpp/signaling_api.cpp :: kRecvBufSize`

#### PROBLEM-015: HB-шторм между клиентом и сервером
- **Проблема:** при получении серверного HB клиент вызывал `send_client_hb()`,
  отправляя ответный HB через багнутый `send_message()` → сервер видел
  нулевой magic → сбрасывал соединение → клиент аварийно завершался.
- **Решение:** `on_server_request(MT_HB)` больше не эхо-отвечает на серверный
  HB. Клиент отправляет HB по своему таймеру, достаточно.
- **Файл:** `lunaricorn/cpp/signaling_api.cpp :: on_server_request()`

#### PROBLEM-015: Неинициализированный MessageHeader в push()
- **Проблема:** `MessageHeader header;` без инициализации — поля содержали
  мусор (UB на некоторых оптимизациях).
- **Решение:** `MessageHeader header{}` — zero-инициализация.
- **Файл:** `lunaricorn/cpp/signaling_api.cpp :: push()`

#### PROBLEM-015: Несоответствие ключей toDict() и on_pub_request()
- **Проблема:** `SignalingEvent::toDict()` генерирует ключи `"message"` и
  `"client_id"`, а `RE_Client::on_pub_request()` искал `"payload"` и `"source"`.
  Сервер не мог обработать push-события от клиента.
- **Решение:** `on_pub_request()` теперь ищет `"message"` (с fallback на
  `"payload"`), `"client_id"` (с fallback на `"source"`).
- **Файл:** `services/signaling_cpp/app/raw_endpoint_client.cpp :: on_pub_request()`

#### PROBLEM-016: _thread_state не сохранялся в SignalingConnector::start()
- **Проблема:** `_thread_state` оставался `nullptr` после `start()`, так как
  переменная создавалась только в лямбде для `runner()`. `stop_runner()`
  проверяла `_thread_state->finished` и падала с `null state`.
- **Решение:** добавлено `_thread_state = std::make_shared<ThreadState>()` в `start()`.
- **Файл:** `lunaricorn/cpp/signaling_api.cpp :: start()`

#### PROBLEM-017: Неинициализированные MessageHeader во всех обработчиках RE_Client
- **Проблема:** `on_heartbeat()`, `on_pub_request()`, `on_query_request()`,
  `on_subscription()` создавали `MessageHeader hdr;` без инициализации
  `magic` и `version`. `serializeJson()` не перезаписывает эти поля —
  только `data_type`, `data_len`, `crc`. Сервер отправлял ответы с
  мусорным magic/version, клиент не мог разобрать пакет →
  `cannot create std::vector larger than max_size()`.
- **Решение:** все `MessageHeader` инициализированы designated initializers
  с корректными `HeaderMagic` и `PROTOCOL_VERSION`.
- **Файл:** `services/signaling_cpp/app/raw_endpoint_client.cpp`

#### on_data(): return → continue для recovery после ошибки парсинга
- **Проблема:** при ошибке magic/version/data_len в `on_data()` вызывался
  `return` вместо `continue`, прерывая обработку оставшихся байт в буфере.
- **Решение:** заменены на `continue`, что позволяет продолжить парсинг
  следующего пакета в том же буфере.
- **Файл:** `lunaricorn/cpp/signaling_api.cpp :: on_data()`

### Добавлено

#### Тесты Raw Connection API (`services/signaling_cpp/test_client/raw_connection_test.cpp`)
- **Тесты SignalingProto**:
  - `Proto_InitialState` — проверка начальной статистики
  - `Proto_SerializeHeartbeat` — сериализация heartbeat
  - `Proto_SerializePushRequest` — сериализация push запроса
  - `Proto_SerializeResponse` — сериализация ответа
  - `Proto_DeserializeWrongMagic` — ошибка неправильного magic
  - `Proto_DeserializeWrongVersion` — ошибка неправильной версии
  - `Proto_DeserializeNonJsonContent` — ошибка не-JSON контента
  - `Proto_DeserializeEmptyBuffer` — ошибка пустого буфера
  - `Proto_StatsTracking` — отслеживание статистики
- **Тесты SignalingEvent**:
  - `SignalingEvent_ToDictFromDict` — конвертация в/из dict
  - `SignalingEvent_PushRequestMakeHeader` — создание заголовка
- **Тесты SignalingPushRequest**:
  - `SignalingPushRequest_MakeHeaderZeroSeq` — заголовок с seq=0
- **Тесты SignalingSubEvent**:
  - `SignalingSubEvent_Build` — успешная сборка
  - `SignalingSubEvent_BuildInvalid` — ошибка сборки
- **Тесты SignalingResponse**:
  - `SignalingResponse_Default` — значения по умолчанию
  - `SignalingResponse_WithSeq` — с seq
  - `SignalingResponse_Comparison` — сравнение
- **Тесты MessageHeader**:
  - `MessageHeader_Size` — проверка размера
  - `MessageHeader_DefaultValues` — значения по умолчанию
- **Тесты констант**:
  - `ProtocolConstants` — заголовочные константы
  - `MessageTypeValues` — типы сообщений
  - `ContentTypeValues` — типы контента
  - `SignalingEventTypes` — типы событий
  - `SignalingEventTags` — теги событий
- **Интеграционные тесты SignalingConnector**:
  - `INTEGRATION_ConnectToServer` — подключение к серверу
  - `INTEGRATION_PushEventAndGetResponse` — push и ответ
  - `INTEGRATION_HeartbeatMechanism` — механизм heartbeat
  - `INTEGRATION_DisconnectHandling` — обработка отключения
  - `INTEGRATION_MultiplePushes` — множественные push
  - `INTEGRATION_InvalidHostFails` — ошибка при неправильном хосте
  - `INTEGRATION_EmptyHostFails` — ошибка пустого хоста
  - `INTEGRATION_ZeroPortFails` — ошибка нулевого порта
  - `INTEGRATION_SubscriptionReceivesEvents` — получение подписок
  - `INTEGRATION_ConnectorReadyState` — состояние ready
  - `INTEGRATION_PushWithInvalidEvent` — push с неверным событием
  - `INTEGRATION_SeqIncrement` — инкремент seq
- **Тесты утилит lunaricorn**:
  - `EventQueue_PushGetAll` — push и get_all
  - `EventQueue_EmptyAfterGetAll` — пустая очередь
  - `EventQueue_ThreadSafety` — потокобезопасность
  - `Counted_IncrementDecrement` — подсчёт живых объектов

#### CMakeLists.txt
- Добавлен `raw_connection_test.cpp` в target `signaling_test`
- Добавлен include path `/opt/app/lunaricorn/cpp/`

### Изменены файлы

- `services/signaling_cpp/test_client/raw_connection_test.cpp` — создано
- `services/signaling_cpp/test_client/CMakeLists.txt` — обновлено
- `services/signaling_cpp/app/raw_endpoint.cpp` — исправлен `stop()` и `start()`
- `doc/CHANGELOG.md` — обновлено

### Изменено

#### RE_Client (raw_endpoint_client)
- Убраны клиентские специфичные вещи из RE_Client
- Удалено:
  - `_pending_responses` — очередь ожидаемых ответов (клиентская логика)
  - `_seq` — счётчик последовательности для клиентских запросов
  - `send_client_hb()` — отправка heartbeat клиенту
  - `connect_time_delay()` / `update_connect_time()` — отслеживание времени подключения
  - `ClientDisconnectCallback` — клиентский колбэк
  - `set_disconnect_callback()` — метод установки колбэка
- Добавлены пустые обработчики для серверных типов сообщений:
  - `on_heartbeat()` — обработчик MT_HB
  - `on_pub_request()` — обработчик MT_PubReq
  - `on_query_request()` — обработчик MT_QueryReq
  - `on_subscription()` — обработчик MT_Sub
- `on_message()` теперь dispatch по типу сообщения к соответствующему обработчику
- RawEndpoint обновлён: убраны вызовы `update_connect_time()` и `set_disconnect_callback()`

## 2026-06-28

### Добавлено

#### Документация
- Создана полная структура документации в `/doc/`
  - `STATUS.md` — текущее состояние проекта
  - `ARCHITECTURE.md` — архитектурная документация
  - `PROBLEMS.md` — реестр известных проблем
  - `ROADMAP.md` — дорожная карта развития
  - `orb.md` — документация Orb сервиса (существующая)

#### Signaling C++ Service
- **SignalingEngine** — ядро обработки событий
  - Создание событий (`createEvent`)
  - Получение уникальных значений (`getUniqueValues`)
  - Поиск событий с фильтрацией (`findEvents`)
  - Поиск событий по типу (`findEventsByType`)

- **MessageStorage** — слой хранения данных
  - Абстракция над PostgreSQL

- **EventDataExtendedTypeHandler** — обработчик расширенных типов
  - `event_data_extended_type_handler.h`
  - `event_data_extended_type_handler.cpp`

- **RawEndpoint** — сырой API эндпоинт
  - `raw_endpoint.h`
  - `raw_endpoint.cpp`
  - `raw_endpoint_client.h`

- **SignalingEngineTest** — механизм selftest
  - `signaling_engine_test.h`
  - `signaling_engine_test.cpp`

- **Система логирования MLog**
  - `maintenance.h` — LogCollectorClient и MLog
  - `mlog.cpp` — реализация логирования
  - Макросы: MLOG, MLOG_D, MLOG_W, MLOG_E, MBUG, MBUG_IF

- **Dockerfile для C++ сервиса**
  - `Dockerfile` — основной образ
  - `Dockerfile.base` — базовый образ
  - `Dockerfile.builder` — образ сборки
  - `Dockerfile.tester` — образ тестирования

- **Скрипты**
  - `it.sh` — скрипт интеграционных тестов
  - `make_app.sh` — сборка приложения
  - `make_base.sh` — сборка базового образа
  - `make_test.sh` — сборка тестового образа

#### Общие библиотеки
- **C++ Library (`lunaricorn/cpp/`)**
  - `maintenance.h` — LogCollectorClient и MLog
  - `mlog.cpp` — реализация MLog
  - DbConfig — конфигурация БД

### Добавлено

#### SignalingEngine — система подписок
- **subscribe()** — создание подписки клиента с фильтрами
  - `client_id` — уникальный идентификатор клиента
  - `types` — фильтр по типу события
  - `sources` — фильтр по источнику
  - `affected` — фильтр по affected
  - `tags` — фильтр по тегам
- **unsubscribe()** — удаление подписки по client_id
- **setOnSubEvent()** — установка колбэка для уведомлений
- **dispatchEvent()** — публичный метод для обработки события
- **on_event()** — приватный метод уведомления подписчиков
- **Subscriber** — структура подписчика с фильтрами и счётчиком
- **Система фильтрации** — проверка всех фильтров (type, source, affected, tags)

#### SignalingEngineTest — тесты подписок
- **testSubscribeUnsubscribe()** — тест создания/удаления подписок
- **testOnEventCallback()** — тест колбэка при совпадении фильтров
- **testOnEventFiltering()** — тест фильтрации по всем типам фильтров

### Изменено

#### SignalingEngine
- Убран `limit` из `Subscriber` и `subscribe()`
- Добавлен `#include <functional>`
- Добавлен `onSubEvent_` член

#### SignalingEngineTest
- Добавлены 3 новых теста для системы подписок

#### Документация
- `Design.md` — обновлена диаграмма потока подписок
- `Design.md` — добавлена структура Subscriber
- `Design.md` — добавлена диаграмма потоков подписок

#### Orb Service
- Добавлена детальная документация в `doc/orb.md`
- Структура:
  - `main.py` — точка входа
  - `app.py` — FastAPI каркас
  - `node_config.py` — регистрация узла
  - `logger_config.py` — логирование
  - `internal/orb_types.py` — конфигурация

#### Docker инфраструктура
- `docker-compose.yaml` — конфигурация всех сервисов
- Скрипты запуска отдельных сервисов
  - `leader_up.sh`
  - `signaling_up.sh`
  - `orb_up.sh`
  - `portal_up.sh`
  - `up.sh`
  - `down.sh`

### Известные проблемы
- Добавлено 12 проблем в `PROBLEMS.md`
- Критическая: PROBLEM-006 — нет схемы БД

---

## 2026-01-01 (предыдущие изменения)

### Добавлено
- Базовая структура проекта
- Leader сервис (Python + FastAPI)
- Signaling сервис (Python + ZeroMQ)
- Orb сервис (Python + FastAPI каркас)
- Portal сервис (Python + FastAPI)
- Maintenance сервис (C++ + Poco)
- PostgreSQL база данных
- Общая C++ библиотека
- Общая Python библиотека
- Docker контейнеризация
- Базовая сеть кластера

### Изменено
- Инициализация репозитория
- Начальная настройка проекта

---

## Статус версий

| Версия | Дата | Статус |
|--------|------|--------|
| 0.3 | 30.06.2026 | Исправление RawEndpoint stop/start |
| 0.2 | 28.06.2026 | Текущая разработка |
| 0.1 | 2026-01-01 | Архивная |

---

*Последнее обновление: 30.06.2026*