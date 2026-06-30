# Журнал изменений проекта Lunaricorn

## 2026-06-30

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
| 0.3 | 29.06.2026 | Система подписок |
| 0.2 | 28.06.2026 | Текущая разработка |
| 0.1 | 2026-01-01 | Архивная |

---

*Последнее обновление: 28.06.2026*