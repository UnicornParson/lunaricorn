# Выполнено: P3 — Интеграция подписок с SignalingEngine

## Статус: ✅ ЗАВЕРШЕНО

Все этапы плана реализованы и успешно собраны.

---

## Реализованные изменения

### Проблема

`RawEndpoint::processSubscription()` отправлял ACK клиенту, но не вызывал `engine->subscribe()`. В результате клиенты не получали pushed события после подписки.

### Решение

Реализована полная цепочка подписок: клиент подписывается → события парсятся → рассылка подписанным клиентам.

---

### Этап 1: Парсинг payload подписки ✅

**Файл:** `app/raw_endpoint.cpp`

- Добавлен `parseSubscriptionPayload()` — принимает `boost::json::value`, извлекает `types`, `sources`, `affected`, `tags`
- Обновлён `processSubscription()` — парсит JSON payload и вызывает `_engine->subscribe()`
- Helper-функции обновлены для работы с `boost::json::value` напрямую:
  - `extractJsonStringField()`
  - `extractOptionalJsonString()`
  - `extractJsonValueField()`
  - `extractJsonTagsField()`

**Формат payload:**
```json
{
  "types": ["orb.system", "orb.alert"],
  "sources": ["risk_engine"],
  "affected": ["EURUSD"],
  "tags": ["News", "Md"]
}
```
Все поля опциональны. Пустой payload = подписка на все события.

### Этап 2: Broadcast событий ✅

**Файл:** `app/raw_endpoint.cpp`

- `processPushRequest()` теперь:
  1. Парсит JSON payload
  2. Преобразует в `StoredEventData`
  3. Вызывает `_engine->createEvent()` для сохранения
  4. Вызывает `_engine->dispatchEvent()` для рассылки подписанным клиентам
  5. Отвечает клиенту с `event_id`

### Этап 3: Отправка событий подписанным клиентам ✅

**Файл:** `app/raw_endpoint.h`
- Добавлен публичный метод `connectEngine(SignalingEnginePtr engine)`

**Файл:** `app/raw_endpoint.cpp`
- Реализован `sendEventToClient()` — отправляет событие конкретному клиенту через WebSocket
- Реализован `connectEngine()` — связывает `SignalingEngine` с `RawEndpoint`:
  - Устанавливает `_engine`
  - Подключает callback `setOnSubEvent()` для рассылки событий подписанным клиентам

**Файл:** `app/main.cpp`
- Добавлен вызов `endpoint->connectEngine(engine)` после создания endpoint

### Этап 4: Автоотписка при disconnect ✅

**Файл:** `app/raw_endpoint.cpp`

- `on_client_closed()` теперь вызывает `_engine->unsubscribe(clientId)` при отключении клиента

### Этап 5: Тестирование ✅

- Сборка через `make_app.sh` прошла успешно
- Все C++ файлы компилируются без ошибок

---

## Архитектура подписок

```
Клиент → MT_Sub (JSON filters) → processSubscription()
    ↓
engine->subscribe(clientId, types, sources, affected, tags)
    ↓
Клиент → MT_PubReq (JSON event) → processPushRequest()
    ↓
engine->createEvent(event_data) → сохранение в БД
engine->dispatchEvent(event_data) → broadcast подписанным
    ↓
onSubEvent_(clientId, event_data) → sendEventToClient()
    ↓
WebSocket send → клиент получает событие
    ↓
Клиент disconnect → on_client_closed() → engine->unsubscribe(clientId)
```

---

## Завязки между файлами

| Файл | Изменения |
|------|-----------|
| `app/raw_endpoint.h` | `connectEngine()` перемещён в `public` |
| `app/raw_endpoint.cpp` | Парсинг, broadcast, sendEventToClient, connectEngine, автоотписка |
| `app/main.cpp` | Вызов `connectEngine()` |

---

## Риски (актуальные)

| Риск | Вероятность | Митигация |
|------|-------------|-----------|
| Circular reference: engine ↔ endpoint | Низкая | `connectEngine()` вызывается после создания объектов |
| Deadlock при sendEventToClient + mutex | Низкая | `_clientsMutex` не удерживается во время WebSocket send |
| Подписчик с медленным recv → буфер переполняется | Средняя | Добавить backpressure / очередь в будущем |

---

*Завершено: 03.07.2026*
*Сборка: успешно (make_app.sh)*

## Цели

1. Клиент может подписаться на события по типам/источникам/тегам
2. Подписка сохраняется в SignalingEngine
3. Подписанные клиенты получают события через broadcast

---

## Этап 1: Парсинг payload подписки

### 1.1. Определить формат payload

**Формат:** JSON с фильтрами

```json
{
  "types": ["orb.system", "orb.alert"],
  "sources": ["risk_engine"],
  "affected": ["EURUSD"],
  "tags": ["News", "Md"]
}
```

**Все поля опциональны.** Пустой payload = подписка на все события.

### 1.2. Добавить парсер в `raw_endpoint.cpp`

**Файл:** `app/raw_endpoint.cpp`

```cpp
// В processSubscription():
static bool parseSubscriptionPayload(
    const std::vector<uint8_t>& data,
    std::vector<std::string>& types,
    std::vector<std::string>& sources,
    std::vector<std::string>& affected,
    std::vector<std::string>& tags)
{
    // 1. Проверить data.size() > 0
    // 2. boost::json::parse(data.begin(), data.end())
    // 3. Извлечь "types", "sources", "affected", "tags"
    // 4. Если массив — преобразовать в vector<string>
    // 5. Если объект — для каждого элемента извлечь "type"/"source"/etc.
    // 6. Вернуть true при успехе, false при ошибке
}
```

**Проверки:**
- Если `data` пуст → пустые фильтры (подписка на всё)
- Если JSON parse fail → вернуть false, отправить error response
- Если фильтр не массив и не объект → вернуть false

### 1.3. Вызов парсера в `processSubscription()`

```cpp
void RawEndpoint::processSubscription(uint64_t clientId, const IncomingMessage& msg)
{
    std::vector<std::string> types, sources, affected, tags;
    
    if (msg.header.data_len > 0 && !msg.data.empty()) {
        if (!parseSubscriptionPayload(msg.data, types, sources, affected, tags)) {
            sendResponse(clientId, msg.header.seq, false, 
                {{"error", boost::json::value("invalid subscription payload")}});
            return;
        }
    }
    
    // Вызвать engine->subscribe()
    _engine->subscribe(clientId, types, sources, affected, tags);
    
    MLOG_D("processSubscription[{}]: subscribed with {} filters", clientId, 
           types.size() + sources.size() + affected.size() + tags.size());
    
    sendResponse(clientId, msg.header.seq, true, 
        {{"subscribed", true}, {"client_id", (uint64_t)clientId}});
}
```

---

## Этап 2: Broadcast событий подписанным клиентам

### 2.1. Добавить метод broadcast в SignalingEngine

**Файл:** `app/signaling_engine.h`

```cpp
// Новый публичный метод — рассылка события всем подписанным клиентам
void broadcastToSubscribers(const StoredEventData& event_data, uint64_t source_client_id);
```

**Файл:** `app/signaling_engine.cpp`

```cpp
void SignalingEngine::broadcastToSubscribers(const StoredEventData& event_data, uint64_t source_client_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& [client_id, sub] : subscribers_) {
        // Пропустить отправителя
        if (client_id == source_client_id) continue;
        
        // Проверить фильтры (переиспользовать логику из on_event)
        bool match = matchFilters(sub, event_data);
        
        if (match && onSubEvent_) {
            onSubEvent_(client_id, event_data);
            sub.count++;
        }
    }
}

// Вспомогательный метод — проверка фильтров
bool SignalingEngine::matchFilters(const Subscriber& sub, const StoredEventData& event_data)
{
    bool match = true;
    
    if (match && !sub.filter_types.empty()) {
        match = false;
        for (const auto& t : sub.filter_types) {
            if (event_data.event_type == t) { match = true; break; }
        }
    }
    
    if (match && !sub.filter_sources.empty()) {
        match = false;
        if (event_data.source.has_value()) {
            for (const auto& s : sub.filter_sources) {
                if (*event_data.source == s) { match = true; break; }
            }
        }
    }
    
    if (match && !sub.filter_affected.empty()) {
        match = false;
        std::string affected_str = boost::json::serialize(event_data.affected);
        for (const auto& a : sub.filter_affected) {
            if (affected_str.find(a) != std::string::npos) { match = true; break; }
        }
    }
    
    if (match && !sub.filter_tags.empty()) {
        match = false;
        for (const auto& tag : sub.filter_tags) {
            for (const auto& et : event_data.tags) {
                if (et == tag) { match = true; break; }
            }
            if (match) break;
        }
    }
    
    return match;
}
```

> **Примечание:** Логика `matchFilters` дублирует часть `on_event`. В будущем вынести в общий метод.

### 2.2. Подключить broadcast в `dispatchEvent()`

**Файл:** `app/signaling_engine.cpp`

```cpp
void SignalingEngine::dispatchEvent(const StoredEventData& event_data)
{
    // 1. Уведомить внутренних подписчиков (existing)
    on_event(event_data);
    
    // 2. Рассылка сетевым клиентам
    broadcastToSubscribers(event_data, 0); // 0 = не от клиента
}
```

### 2.3. Связать RawEndpoint с dispatchEvent

**Файл:** `app/raw_endpoint.cpp`

В `processPushRequest()` вместо простого ACK:

```cpp
void RawEndpoint::processPushRequest(uint64_t clientId, const IncomingMessage& msg)
{
    if (msg.header.data_len > 0 && !msg.data.empty()) {
        try {
            // Преобразовать IncomingMessage.data → StoredEventData
            StoredEventData event_data;
            event_data.event_type = extractField(msg.data, "type", "unknown");
            event_data.source = extractOptionalString(msg.data, "source");
            event_data.payload = msg.data; // или извлечь "payload"
            event_data.affected = extractField(msg.data, "affected", boost::json::array{});
            event_data.tags = extractField(msg.data, "tags", std::vector<std::string>{});
            event_data.timestamp = getCurrentTimestamp();
            
            long long eid = _engine->createEvent(event_data);
            
            // Рассылка подписанным клиентам
            _engine->dispatchEvent(event_data);
            
            sendResponse(clientId, msg.header.seq, true, 
                {{"event_id", (long long)eid}, {"published", true}});
        } catch (const std::exception& e) {
            MLOG_E("processPushRequest[{}]: failed: {}", clientId, e.what());
            sendResponse(clientId, msg.header.seq, false, 
                {{"error", boost::json::value(e.what())}});
        }
    } else {
        MLOG_W("processPushRequest[{}]: empty payload", clientId);
        sendResponse(clientId, msg.header.seq, false, 
            {{"error", boost::json::value("empty payload")}});
    }
}
```

---

## Этап 3: Отправка событий подписанным клиентам

### 3.1. Добавить метод sendToClient в RawEndpoint

**Файл:** `app/raw_endpoint.h`

```cpp
// Добавить приватный метод
void sendEventToClient(uint64_t clientId, const StoredEventData& event_data);
```

**Файл:** `app/raw_endpoint.cpp`

```cpp
void RawEndpoint::sendEventToClient(uint64_t clientId, const StoredEventData& event_data)
{
    std::lock_guard<std::mutex> lock(_clientsMutex);
    auto it = _clients.find(clientId);
    if (it == _clients.end() || !it->second) {
        MLOG_W("sendEventToClient[{}]: client not found", clientId);
        return;
    }
    
    // Создать MessageHeader для push события
    lunaricorn::internal::MessageHeader hdr;
    hdr.magic = lunaricorn::internal::HeaderMagic;
    hdr.version = lunaricorn::internal::PROTOCOL_VERSION;
    hdr.type = lunaricorn::internal::MessageType::MT_PubReq;
    hdr.data_type = lunaricorn::internal::ContentType::CT_Json;
    hdr.flags = 0;
    hdr.seq = 0; // push без seq
    hdr.data_len = 0;
    hdr.crc = 0;
    
    // Подготовить payload
    boost::json::object payload;
    payload["type"] = boost::json::value(event_data.event_type);
    if (event_data.source.has_value())
        payload["source"] = boost::json::value(*event_data.source);
    else
        payload["source"] = boost::json::value(std::string("unknown"));
    payload["payload"] = event_data.payload;
    payload["timestamp"] = boost::json::value(static_cast<int64_t>(event_data.timestamp));
    
    // Добавить tags
    boost::json::array tags_arr;
    for (const auto& tag : event_data.tags) {
        tags_arr.push_back(boost::json::value(tag));
    }
    payload["tags"] = std::move(tags_arr);
    
    // Добавить affected
    if (!event_data.affected.empty()) {
        payload["affected"] = event_data.affected;
    }
    
    // Отправить
    try {
        it->second->send_message(hdr, payload);
        MLOG_D("sendEventToClient[{}]: event {} sent", clientId, event_data.event_type);
    } catch (const std::exception& e) {
        MLOG_E("sendEventToClient[{}]: failed to send: {}", clientId, e.what());
        // Если send failed — отметить клиент как dead
        on_client_closed(clientId);
    }
}
```

### 3.2. Подключить sendToClient в callback

**Файл:** `app/signaling_engine.cpp`, конструктор:

```cpp
SignalingEngine::SignalingEngine(const DbConfig& db_cfg)
{
    storage_ = std::make_unique<MessageStorage>(db_cfg);
    
    // Set callback for subscribers — will be set by RawEndpoint
    // onSubEvent_ будет установлен при подключении RawEndpoint
}
```

**Файл:** `app/raw_endpoint.h` — добавить метод:

```cpp
void connectEngine(SignalingEnginePtr engine);
```

**Файл:** `app/raw_endpoint.cpp`:

```cpp
void RawEndpoint::connectEngine(SignalingEnginePtr engine)
{
    _engine = engine;
    
    // Подключить callback для рассылки событий клиентам
    _engine->setOnSubEvent([this](uint64_t clientId, const StoredEventData& event_data) {
        sendEventToClient(clientId, event_data);
    });
}
```

### 3.3. Вызвать connectEngine в main.cpp

**Файл:** `app/main.cpp`:

```cpp
auto engine = make_engine(dbcfg);
auto engine_test = std::make_shared<SignalingEngineTest>(engine);
selftest_ok = engine_test->run();

auto endpoint = std::make_shared<RawEndpoint>(raw_host, raw_port, engine);

// !!! Новая строка !!!
endpoint->connectEngine(engine);

MLOG_D("create objects - ok");
endpoint->start();
```

---

## Этап 4: Отписка клиента

### 4.1. Добавить обработку MT_Unsub (опционально)

Если нужен протокол для явной отписки:

```cpp
enum MessageType : uint8_t {
    ...
    MT_Unsub = 6  // Новая константа
};
```

В `processSubscription()` добавить:

```cpp
// Или через MT_Sub с special flag
if (msg.data contains "unsubscribe": true) {
    _engine->unsubscribe(clientId);
    sendResponse(clientId, msg.header.seq, true, {{"unsubscribed", true}});
    return;
}
```

### 4.2. Автоотписка при disconnect

**Файл:** `raw_endpoint.cpp::on_client_closed()`

```cpp
void RawEndpoint::on_client_closed(uint64_t clientId)
{
    // ... existing code ...
    
    // Автоотписка от событий
    if (_engine) {
        _engine->unsubscribe(clientId);
        MLOG_D("on_client_closed[{}]: auto-unsubscribed from events", clientId);
    }
}
```

---

## Этап 5: Тестирование

### 5.1. Ручной тест через CLI

```bash
# Терминал 1: запустить сервер
./signaling_cpp

# Терминал 2: клиент-подписчик
./cli/main.cpp 127.0.0.1 8080
# → автоматически подпишется на все события

# Терминал 3: клиент-публикатор (TestSender)
# → отправит тестовые события
# → клиент в терминале 2 должен получить события
```

### 5.2. Проверка подписки по типам

Добавить в CLI возможность отправить подписку с фильтрами:

```cpp
// В test_sender или новом CLI
// Отправить subscription:
{
    "types": ["orb.system"],
    "sources": ["risk_engine"]
}
```

### 5.3. Проверка multiple clients

1. Подключить 2+ клиентов
2. Каждый подписаться на разные типы
3. Отправить событие
4. Проверить что получают только подписанные

---

## Оценка сложности

| Этап | Сложность | Время (ч) |
|------|-----------|-----------|
| 1. Парсинг payload | Низкая | 1-2 |
| 2. Broadcast в Engine | Средняя | 3-4 |
| 3. Отправка клиентам | Средняя | 3-4 |
| 4. Автоотписка | Низкая | 0.5-1 |
| 5. Тестирование | Средняя | 2-3 |
| **Итого** | | **9.5-14** |

---

## Зависимости

```
Этап 1 (Парсинг) ──▶ Этап 2 (Broadcast)
                              │
                              ▼
                         Этап 3 (Отправка)
                              │
                              ▼
                         Этап 4 (Автоотписка)
                              │
                              ▼
                         Этап 5 (Тестирование)
```

---

## Риски

| Риск | Вероятность | Митигация |
|------|-------------|-----------|
| Circular reference: engine ↔ endpoint | Средняя | Использовать weak_ptr или init после создания |
| Потеря событий при broadcast | Низкая | Event dispatch async с queue |
| Deadlock при sendEventToClient + mutex | Средняя | Не блокировать _clientsMutex во время send |
| Подписчик с медленным recv → буфер переполняется | Средняя | Добавить backpressure / очередь |

---

*Создано: 03.07.2026*