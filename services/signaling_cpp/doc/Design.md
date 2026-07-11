# Дизайн-документация микросервиса signaling_cpp

## Схема архитектуры системы

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           main.cpp                                       │
│  ┌──────────────────┐    ┌──────────────────────┐                      │
│  │ loadConfigFrom   │───▶│ SignalingEngine      │                      │
│  │ Environment()    │    │  (engine_)           │                      │
│  │ (DbConfig)       │    └──────────┬───────────┘                      │
│  └──────────────────┘               │                                  │
│                                     ▼                                  │
│  ┌──────────────────────┐    ┌──────────────────────┐                  │
│  │ SignalingEngineTest  │───▶│ SignalingEngine::    │                  │
│  │  (endpoint_test)     │    │  storage_            │                  │
│  │  (run())             │    │   (MessageStorage)   │                  │
│  └──────────────────────┘    └──────────┬───────────┘                  │
│                                         │                              │
│                                         ▼                              │
│  ┌──────────────────────────────────────────────────┐                 │
│  │           MessageStorage                         │                 │
│  │  - soci::session sql (PostgreSQL)               │                 │
│  │  - create_event()                                │                 │
│  │  - find_events()                                 │                 │
│  │  - get_unique_values()                           │                 │
│  │  - find_events_by_type()                         │                 │
│  └──────────────────────────────────────────────────┘                 │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Список всех классов

### 1. `Endpoint` (абстрактный базовый класс)

**Файл:** `app/endpoint.h`

**Назначение:** Абстрактный интерфейс для всех типов эндпоинтов (сетевых и HTTP).

**Статус:** ✅ Используется (базовый класс для RawEndpoint)

**Зависимости:**
```
Endpoint
  ├── lunaricorn.h (EndpointStats, Counter)
  └── event_data.h (EventData)
```

**Методы:**
```cpp
class Endpoint
{
public:
    virtual void handleEvent(const EventData& event) = 0;  // Обработка события
    virtual bool start() = 0;                               // Запуск
    virtual bool stop() = 0;                                // Остановка
    const EndpointStats& stats() { return _stats; }
protected:
    EndpointStats _stats;  // Статистика (requests, errors)
};
```

---

### 2. `RawEndpoint`

**Файл:** `app/raw_endpoint.h`

**Назначение:** TCP-сервер для обработки сырых бинарных соединений с клиентами по протоколу Signaling.

**Статус:** ✅ Используется (основной сетевой эндпоинт)

**Зависимости:**
```
RawEndpoint
  ├── Endpoint (базовый класс)
  ├── RE_Client (управление клиентами)
  ├── SignalingProto (протокол)
  └── Poco::Net::ServerSocket (серверный сокет)
```

**Диаграмма классов:**
```
RawEndpoint
  ├── _serverSocket: ServerSocket    ← слушает входящие соединения
  ├── _clients: map<uint64_t, RE_Client_ptr>  ← подключенные клиенты
  ├── _nextId: atomic<uint64_t>     ← генерация ID клиентов
  ├── _stopping: atomic<bool>       ← флаг остановки
  ├── _clientsMutex: mutex          ← синхронизация клиентов
  ├── _acceptThread: thread         ← поток accept
  ├── _handlerThread: thread        ← поток обработки
  └── _proto: SignalingProto        ← протокол обработки
```

**Методы:**
```cpp
class RawEndpoint : public Endpoint
{
public:
    RawEndpoint(const std::string& ip, Poco::UInt16 port);
    ~RawEndpoint();
    bool start()   override;   // Запуск acceptLoop + handleClients
    bool stop()    override;   // Остановка, закрытие клиентов
    void handleEvent(const EventData& event) override;  // Forwarding

private:
    void acceptLoop();                          // Принятие соединений
    void handleClients();                       // Чтение/обработка данных
    void on_client_message(uint64_t, const IncomingMessage&);
    void on_client_closed(uint64_t);
    void send_hb();                             // Периодический heartbeat

    // Обработчики типов сообщений
    void processHeartbeat(uint64_t, const IncomingMessage&);
    void processSubscription(uint64_t, const IncomingMessage&);
    void processPushRequest(uint64_t, const IncomingMessage&);
    void processResponse(uint64_t, const IncomingMessage&);
    void processQueryRequest(uint64_t, const IncomingMessage&);
    void processUnknownMessageType(uint64_t, const MessageHeader&);

    // Отправка ответов
    void sendHeartbeat(uint64_t clientId);
    void sendResponse(uint64_t clientId, uint64_t seq, bool success, const boost::json::object&);
};
```

---

### 3. `RE_Client` (RawEndpoint Client)

**Файл:** `app/raw_endpoint_client.h`

**Назначение:** Представление одного клиентского подключения. Управляет парсингом бинарного протокола, таймингами heartbeat, отправкой сообщений.

**Статус:** ✅ Используется (внутри RawEndpoint)

**Зависимости:**
```
RE_Client
  ├── Poco::Net::StreamSocket (сокет)
  ├── SignalingProto (протокол)
  ├── MessageReadyCallback (callback для сервера)
  └── ClientDisconnectCallback (callback для сервера)
```

**Диаграмма классов:**
```
RE_Client
  ├── sock: StreamSocket                    ← сокет клиента
  ├── _proto: SignalingProto                ← парсер протокола
  ├── _pstate: IncomingPacketState          ← состояние входящего пакета
  ├── _seq: atomic<seq_t>                   ← счётчик последовательности
  ├── _pending_responses: map<seq_t, SignalingResponse>
  ├── _connectTime: time_point              ← время подключения
  ├── _client_hb: time_point                ← последнее HB от клиента
  ├── _server_hb: time_point                ← последнее HB от сервера
  ├── _last_send: steady_clock::time_point  ← последнее отправление
  ├── _id: uint64_t                         ← ID клиента (присвоен сервером)
  ├── _msgCbk: MessageReadyCallback         ← callback для сообщений
  ├── _disconnectCbk: ClientDisconnectCallback  ← callback для disconnect
  └── _count: static atomic<int64_t>        ← счётчик активных клиентов
```

**Методы:**
```cpp
class RE_Client
{
public:
    explicit RE_Client(Poco::Net::StreamSocket socket);
    ~RE_Client();

    // Timing
    connect_time_delay() / client_hb_delay() / server_hb_delay();
    update_connect_time() / update_client_hb() / update_server_hb();

    // Callbacks
    set_message_callback(MessageReadyCallback cb);
    set_disconnect_callback(ClientDisconnectCallback cb);

    // Data processing
    void processData(const std::vector<char>& data);  // Парсинг входящих данных
    void send_client_hb();                              // Отправка heartbeat
    bool send_message(MessageHeader& msg, const boost::json::object& data);

    // Status
    is_silent(std::chrono::seconds threshold);
    socket() / set_id() / get_id();

    static clients_count();  // Статический счётчик
};
```

**IncomingPacketState (вложенная структура):**
```
IncomingPacketState
  ├── header: MessageHeader
  ├── buffer: vector<uint8_t>
  ├── receivedHeaderBytes: size_t
  ├── receivedPayloadBytes: size_t
  ├── headerComplete: bool
  └── reset()  ← сброс состояния
```

---

### 4. `SignalingEngine`

**Файл:** `app/signaling_engine.h`

**Назначение:** Основной движер сервиса. Обёртка над MessageStorage с потокобезопасностью и системой подписок.

**Статус:** ✅ Используется (основной класс приложения)

**Зависимости:**
```
SignalingEngine
  ├── MessageStorage (storage_)  ← хранение данных
  ├── mutex_                     ← синхронизация
  ├── subscribers_               ← карта подписчиков
  └── onSubEvent_                ← колбэк для подписок
```

**Диаграмма классов:**
```
SignalingEngine
  ├── storage_: unique_ptr<MessageStorage>  ← хранилище
  ├── mutex_: mutex                         ← защита concurrent доступа
  ├── subscribers_: map<uint64_t, Subscriber>  ← подписчики
  └── onSubEvent_: function<void(uint64_t, StoredEventData&)>  ← колбэк
```

**Структура Subscriber:**
```
Subscriber
  ├── reg_point: time_point     ← время регистрации
  ├── filter_types: vector<string>   ← фильтр по типу события
  ├── filter_sources: vector<string> ← фильтр по источнику
  ├── filter_affected: vector<string>← фильтр по affected
  ├── filter_tags: vector<string>    ← фильтр по тегам
  ├── client_id: uint64_t           ← ID клиента
  └── count: uint64_t               ← счётчик доставленных событий
```

**Методы:**
```cpp
class SignalingEngine
{
public:
    explicit SignalingEngine(const DbConfig& db_cfg);
    ~SignalingEngine();

    // CRUD операции
    long long createEvent(const StoredEventData& event_data);
    vector<string> getUniqueValues(const string& field_name);
    vector<StoredEventDataExtended> findEvents(double timestamp, ...);
    vector<StoredEventDataExtended> findEventsByType(const string& event_type);

    // Система подписок
    void subscribe(uint64_t client_id, types, sources, affected, tags);
    void unsubscribe(uint64_t client_id);
    void setOnSubEvent(function<void(uint64_t, StoredEventData&)> cb);

private:
    void on_event(const StoredEventData& event_data);  // Уведомляет подписчиков
    mutex_  // Защита всех методов
};
```

**Диаграмма потока подписок:**
```
subscribe(client_id, filters)
  └──▶ lock(mutex_)
       └──▶ subscribers_[client_id] = Subscriber{filters, ...}

unsubscribe(client_id)
  └──▶ lock(mutex_)
       └──▶ subscribers_.erase(client_id)

on_event(event_data)
  └──▶ lock(mutex_)
       └──▶ for each subscriber:
            ├─▶ check filter_types (если не пуст)
            ├─▶ check filter_sources (если не пуст)
            ├─▶ check filter_affected (если не пуст)
            ├─▶ check filter_tags (если не пуст)
            └─▶ if match && onSubEvent_ != null:
                 └──▶ onSubEvent_(client_id, event_data)
                 └──▶ subscriber.count++
```

**Диаграмма потока данных:**
```
createEvent()
  └──▶ lock(mutex_)
       └──▶ storage_->create_event(event_data)
            └──▶ soci::session sql (PostgreSQL)
                 └──▶ INSERT INTO signaling_events

findEvents()
  └──▶ lock(mutex_)
       └──▶ storage_->find_events(timestamp, types, sources, tags, limit)
            └──▶ soci::session sql (PostgreSQL)
                 └──▶ SELECT FROM signaling_events
```

---

### 5. `MessageStorage`

**Файл:** `app/message_storage.h`

**Назначение:** Слой доступа к PostgreSQL. CRUD-операции для signaling_events.

**Статус:** ✅ Используется (внутри SignalingEngine)

**Зависимости:**
```
MessageStorage
  ├── soci::session sql (PostgreSQL)
  ├── soci/postgresql (driver)
  ├── boost::json (JSON обработка)
  └── DbConfig (конфигурация)
```

**Диаграмма классов:**
```
MessageStorage
  ├── sql: soci::session         ← подключение к PostgreSQL
  ├── row_to_event(row)          ← преобразование строки в объект
  ├── json_to_string(value)      ← сериализация JSON
  ├── tags_to_pg_array(tags)     ← конвертация тегов в PG array
  └── pg_array_to_tags(tags_str) ← конвертация PG array в теги
```

**Методы:**
```cpp
class MessageStorage
{
public:
    explicit MessageStorage(const DbConfig& cfg);
    long long create_event(const StoredEventData& event);
    vector<string> get_unique_values(const string& field);
    vector<StoredEventDataExtended> find_events(double timestamp, ...);
    vector<StoredEventDataExtended> find_events_by_type(const string& type);

private:
    row_to_event(soci::row& row);
    json_to_string(const json::value& v);
    tags_to_pg_array(const vector<string>& tags);
    pg_array_to_tags(const string& tags_str);
};
```

---

### 6. `HTTP_Endpoint`

**Файл:** `app/http_endpoint.h`

**Назначение:** HTTP-эндпоинт для обработки REST API запросов.

**Статус:** ❌ НЕ ИСПОЛЬЗУЕТСЯ (заглушка, все методы возвращают false/пусто)

**Диаграмма:**
```
HTTP_Endpoint (STUB)
  ├── Endpoint (базовый класс)
  ├── handleEvent() → {}           ← пустая реализация
  ├── start() → false              ← не работает
  └── stop() → false               ← не работает
```

---

### 7. `SignalingEngineTest`

**Файл:** `app/signaling_engine_test.h`

**Назначение:** Тестовый хarness для проверки SignalingEngine.

**Статус:** ✅ Используется (в main.cpp для self-test)

**Зависимости:**
```
SignalingEngineTest
  ├── engine_: shared_ptr<SignalingEngine>  ← тестируемый движер
  └── testCreateEvent(), testGetUniqueValues(), testFindEvents(), ...
```

**Диаграмма:**
```
SignalingEngineTest
  ├── engine_: shared_ptr<SignalingEngine>
  ├── run() → bool              ← запускает все тесты
  ├── testCreateEvent() → bool
  ├── testGetUniqueValues() → bool
  ├── testFindEvents() → bool
  ├── testFindEventsByType() → bool
  └── testDatabaseInitialization() → bool
```

---

### 8. `EventDataExtended`

**Файл:** `lunaricorn/cpp/event_data.h`

**Назначение:** Расширенная версия EventData с дополнительными полями.

**Статус:** ✅ Используется (в TypeHandler для Poco::Data)

---

### 9. `StoredEventData`

**Файл:** `app/message_storage.h`

**Назначение:** Структура данных для хранения события.

**Статус:** ✅ Используется (в MessageStorage и SignalingEngine)

```
StoredEventData
  ├── event_type: string
  ├── payload: json::value
  ├── timestamp: double
  ├── source: optional<string>
  ├── affected: json::value
  └── tags: vector<string>
```

---

### 10. `StoredEventDataExtended`

**Файл:** `app/message_storage.h`

**Назначение:** StoredEventData + ID события. Наследуется от StoredEventData.

**Статус:** ✅ Используется (результат запросов)

```
StoredEventDataExtended : StoredEventData
  └── eid: long long
```

---

### 11. `BrokenStorageError`

**Файл:** `app/message_storage.h`

**Назначение:** Исключение при ошибках хранилища.

**Статус:** ✅ Используется (в MessageStorage::MessageStorage)

```
BrokenStorageError : std::runtime_error
```

---

### 12. `TypeHandler<EventDataExtended>`

**Файл:** `app/event_data_extended_type_handler.h`

**Назначение:** Poco::Data type handler для EventDataExtended.

**Статус:** ⚠️ Определён, но НЕ ИСПОЛЬЗУЕТСЯ (MessageStorage используетsoci напрямую)

---

## Внешние зависимости

### DbConfig (lunaricorn/cpp/config.h)

```
DbConfig
  ├── dbType: string
  ├── dbHost: string
  ├── dbPort: string
  ├── dbUser: string
  ├── dbPassword: string
  ├── dbDbname: string
  ├── valid() → bool
  └── toStr() → string
```

### EventData (lunaricorn/cpp/event_data.h)

```
EventData
  ├── event_type: string
  ├── payload: json::value
  ├── timestamp: double
  ├── source: optional<string>
  ├── affected: json::value
  └── tags: vector<string>
```

### MLog / LogCollectorClient (lunaricorn/cpp/maintenance.h)

```
MLog (static logging)
  ├── owner: string
  ├── token: string
  ├── is_stub: bool
  ├── log() / d() / w() / e()

LogCollectorClient (singleton)
  ├── base_url_: string
  ├── timeout_: int
  ├── max_retries_: int
  ├── get_status()
  ├── health_check()
  ├── send_log()
  ├── send_logs_batch()
  └── pull_logs()
```

### lunaricorn::internal::SignalingProto (proto/signaling.h)

```
SignalingProto
  ├── serializeJson()
  ├── deserialize()
  └── manages MessageHeader, MessageType, ContentType
```

---

## Схема взаимосвязей между классами

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              main()                                          │
│                                                                              │
│  1. loadConfigFromEnvironment() → DbConfig                                  │
│  2. SignalingEngine(dbcfg) ──▶ MessageStorage(sql)                         │
│  3. SignalingEngineTest(engine) ──▶ run()                                   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘

КЛАССЫ ИХ ЗАВИСИМОСТИ:

SignalingEngine
  ├── uses ──▶ MessageStorage (storage_)
  ├── uses ──▶ DbConfig (constructor param)
  ├── uses ──▶ StoredEventData (method param)
  ├── uses ──▶ StoredEventDataExtended (return type)
  └── uses ──▶ MLOG/MLOG_E (logging)

MessageStorage
  ├── uses ──▶ soci::session (PostgreSQL)
  ├── uses ──▶ DbConfig (constructor param)
  ├── uses ──▶ boost::json (JSON parsing)
  ├── uses ──▶ StoredEventData (method param)
  ├── uses ──▶ StoredEventDataExtended (return type)
  ├── uses ──▶ BrokenStorageError (exception)
  └── uses ──▶ tags_to_pg_array / pg_array_to_tags (helpers)

RawEndpoint
  ├── inherits ──▶ Endpoint
  ├── uses ──▶ RE_Client (client management)
  ├── uses ──▶ SignalingProto (_proto)
  ├── uses ──▶ EventData (handleEvent param)
  ├── uses ──▶ IncomingMessage (protocol)
  ├── uses ──▶ MessageHeader (protocol)
  ├── uses ──▶ MessageType (enum)
  ├── uses ──▶ ContentType (enum)
  └── uses ──▶ MLOG/MLOG_E (logging)

RE_Client
  ├── uses ──▶ SignalingProto (_proto)
  ├── uses ──▶ IncomingMessage (parsing)
  ├── uses ──▶ MessageHeader (protocol)
  ├── uses ──▶ SignalingResponse (pending)
  ├── uses ──▶ MessageReadyCallback (server callback)
  └── uses ──▶ ClientDisconnectCallback (server callback)

HTTP_Endpoint
  ├── inherits ──▶ Endpoint
  └── [НЕ ИСПОЛЬЗУЕТСЯ - заглушка]

SignalingEngineTest
  ├── uses ──▶ SignalingEngine (engine_)
  ├── uses ──▶ testCreateEvent()
  ├── uses ──▶ testGetUniqueValues()
  ├── uses ──▶ testFindEvents()
  ├── uses ──▶ testFindEventsByType()
  ├── uses ──▶ testSubscribeUnsubscribe()
  ├── uses ──▶ testOnEventCallback()
  └── uses ──▶ testOnEventFiltering()

TypeHandler<EventDataExtended>
  ├── uses ──▶ EventDataExtended (template param)
  ├── uses ──▶ Poco::Data::RecordSet
  └── ⚠️ НЕ ИСПОЛЬЗУЕТСЯ в текущем коде
```

---

## Схема жизненного цикла

```
main()
  │
  ├─▶ loadConfigFromEnvironment()
  │     └─▶ DbConfig (host, port, user, password, dbname)
  │
  ├─▶ SignalingEngine(dbcfg)
  │     └─▶ MessageStorage(dbcfg)
  │           └─▶ soci::session.open(postgresql, conn_str)
  │
  ├─▶ SignalingEngineTest(engine)
  │     └─▶ run()
  │           ├─▶ testCreateEvent()
  │           ├─▶ testGetUniqueValues()
  │           ├─▶ testFindEvents()
  │           ├─▶ testFindEventsByType()
  │           └─▶ testDatabaseInitialization()
  │
  └─▶ exit
```

---

## Классы которые НЕ ИСПОЛЬЗУЮТСЯ

| Класс | Файл | Причина |
|-------|------|---------|
| `HTTP_Endpoint` | `app/http_endpoint.h/cpp` | Заглушка: все методы возвращают false/пусто, не используется в main.cpp |
| `TypeHandler<EventDataExtended>` | `app/event_data_extended_type_handler.h` | Определён, но MessageStorage использует soci напрямую без Poco::Data |

---

## Схема данных PostgreSQL

```
таблица: signaling_events
┌──────────┬──────────────┬──────────────┬─────────────────┐
│ поле      │ тип          │ описание     │ примечание      │
├──────────┼──────────────┼──────────────┼─────────────────┤
│ eid      │ BIGSERIAL    │ PRIMARY KEY  │ автоинкремент   │
│ type     │ TEXT         │ event_type   │                 │
│ payload  │ JSONB        │ данные       │                 │
│ affected │ JSONB        │ затронутые   │                 │
│ ctime    │ TIMESTAMP    │ created_at   │ to_timestamp()  │
│ owner    │ TEXT         │ source       │ "ownerless"     │
│ tags     │ TEXT[]       │ теги         │ PG array        │
└──────────┴──────────────┴──────────────┴─────────────────┘
```

---

## Протокол Signaling (binary protocol)

```
MessageHeader (fixed size)
┌──────────┬───────────┬──────────┬────────────┬───────┬────────┬──────┐
│ magic    │ version   │ type     │ data_type  │ flags │ seq    │ data │
│ uint32   │ uint8     │ uint8    │ uint8      │ uint8 │ uint64 │ len  │
└──────────┴───────────┴──────────┴────────────┴───────┴────────┴──────┘
                                                    │
                                                    ▼
payload (data_len bytes) → JSON (if CT_Json)

MessageType:
  MT_Hb       → heartbeat
  MT_Sub      → subscription
  MT_PubReq   → push request
  MT_Response → response
  MT_QueryReq → query request
```

---

## Итоговая сводка

### ✅ Используемые классы (8):

| Класс | Роль |
|-------|------|
| `SignalingEngine` | Основной движер сервиса |
| `MessageStorage` | Слой доступа к PostgreSQL |
| `RawEndpoint` | TCP-сервер |
| `RE_Client` | Клиентское подключение |
| `StoredEventData` | Структура данных события |
| `StoredEventDataExtended` | Структура с ID |
| `SignalingEngineTest` | Тестовый хarness |
| `BrokenStorageError` | Исключение |

### ❌ Не используемые классы (2):

| Класс | Причина |
|-------|---------|
| `HTTP_Endpoint` | Заглушка (stub) |
| `TypeHandler<EventDataExtended>` | Не подключён |

### 📦 Внешние библиотеки:

| Библиотека | Назначение |
|------------|------------|
| soci + soci-postgresql | Доступ к PostgreSQL |
| boost::json | JSON сериализация |
| Poco::Net | Сетевые сокеты |
| Poco::Data | Data API (частично) |
| lunaricorn (shared lib) | DbConfig, EventData, MLog |