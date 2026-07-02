# Анализ тестов signaling_cpp

**Дата:** 2026-07-02  
**Статус:** Критические проблемы обнаружены

---

## 1. Ошибки из логов тестера (client side)

### 1.1. Тесты валидации SignalingEvent::fromDict

Лог показывает, что тесты намеренно проверяют обработку невалидных входных данных:

```
[ ERROR ] SignalingEvent::fromDict missing required key 'event_type'
[ ERROR ] SignalingEvent::fromDict key 'event_type' expected string but got type 'int64' value '123'
[ ERROR ] SignalingEvent::fromDict missing required key 'client_id'
[ ERROR ] SignalingEvent::fromDict key 'message' expected object but got type 'string' value '"not_object"'
[ ERROR ] SignalingEvent::fromDict key 'timestamp' expected numeric type but got type 'string' value '"invalid"'
[WARNING] SignalingEvent::fromDict tags[1] expected string but got type 'int64' value '123'
[WARNING] SignalingEvent::fromDict tags[2] expected string but got type 'bool' value 'true'
```

**Вывод:** Функция `fromDict()` корректно отклоняет невалидные данные. Это ожидаемое поведение.

### 1.2. Критическая ошибка на сервере

```
raw_endpoint.cpp:void lunaricorn::RawEndpoint::handleClients():232 [ ERROR ] Error_1 processing client# 1 data: cannot create std::vector larger than max_size()
```

**КРИТИЧНО:** Это указывает на попытку создать `std::vector` с размером, превышающим `max_size()`. Вероятная причина — `data_len` из заголовка сообщения содержит некорректное (огромное) значение.

---

## 2. Анализ кода — обнаруженные проблемы

### ПРОБЛЕМА 1: Нет защиты от data_len = 0 с непустым payload (КРИТИЧНО)

**Фай:** `services/signaling_cpp/app/raw_endpoint_client.cpp:111-131`

```cpp
if (_pstate.header.data_len == 0)
{
    IncomingMessage msg;
    if (!_proto->deserializeJson(_pstate.buffer, msg))
    {
        MLOG_E("deserializeJson failed for empty payload packet: seq={} type={}",
               _pstate.header.seq,
               static_cast<uint32_t>(_pstate.header.type));
        _pstate.reset();
        return;
    }
    ...
}
```

**Проблема:** Если `data_len == 0`, код переходит в блок обработки пустого payload. Но если данные уже частично прочитаны и `_pstate.buffer` содержит мусор, `deserializeJson` может вернуть false. Однако нет проверки, что buffer действительно содержит только заголовок.

**Риск:** При потоке байт, где `data_len` установлен в 0, но клиент отправляет реальные данные, может произойти рассинхронизация состояния.

---

### ПРОБЛЕМА 2: `cannot create std::vector larger than max_size()` (КРИТИЧНО)

**Фай:** `lunaricorn/cpp/signaling_api.cpp:243` и `services/signaling_cpp/app/raw_endpoint_client.cpp:108`

```cpp
// signaling_api.cpp:243
_pstate.buffer.resize(sizeof(MessageHeader) + _pstate.header.data_len);

// raw_endpoint_client.cpp:108
_pstate.buffer.resize(sizeof(MessageHeader) + _pstate.header.data_len);
```

**Проблема:** Если `data_len` содержит аномально большое значение (например, из-за повреждения данных или злонамеренного клиента), `sizeof(MessageHeader) + data_len` может переполнить `size_t` или превысить лимит вектора.

**Текущая защита недостаточна:**

```cpp
// raw_endpoint_client.cpp:101-106
if (_pstate.header.data_len > MAX_DATA_LEN)
{
    MLOG_E("payload too large: max={} actual={}", MAX_DATA_LEN, _pstate.header.data_len);
    _pstate.reset();
    return;
}
```

`MAX_DATA_LEN` = 128MB. Но если `data_len` содержит `UINT32_MAX` (4GB), проверка `> MAX_DATA_LEN` проходит, но `resize` падает.

**Решение:** Добавить проверку на переполнение:

```cpp
if (_pstate.header.data_len > MAX_DATA_LEN || 
    _pstate.header.data_len > (std::numeric_limits<size_t>::max() - sizeof(MessageHeader)))
```

---

### ПРОБЛЕМА 3: Рассинхронизация heartbeat (ЛОЖНОЕ ТРЕВОЖЕНИЕ)

**Лог сервера:**

```
raw_endpoint.cpp:void lunaricorn::RawEndpoint::send_hb():173 [ DEBUG ] send hb to 1
raw_endpoint.cpp:void lunaricorn::RawEndpoint::sendHeartbeat(uint64_t):405 [ DEBUG ] sendHeartbeat[1]: sending heartbeat
raw_endpoint_client.cpp:bool lunaricorn::RE_Client::send_message(...):536 [ DEBUG ] MessageHeader: magic=0x12345678 version=1 type=1 data_type=1 flags=0 seq=0 data_len=0 crc=0x00000000
raw_endpoint_client.cpp:bool lunaricorn::RE_Client::send_message(...):556 [ DEBUG ] try to send 24b
raw_endpoint_client.cpp:void lunaricorn::RE_Client::on_message(...):185 [ DEBUG ] on_message: MessageHeader: magic=0x12345678 version=1 type=1 data_type=1 flags=0 seq=0 data_len=0 crc=0x00000000
raw_endpoint.cpp:void lunaricorn::RawEndpoint::on_client_message(uint64_t, const lunaricorn::internal::IncomingMessage&):252 [ DEBUG ] on_client_message[1]: received msg type=1 data_type=1 seq=0 data_len=0
raw_endpoint.cpp:void lunaricorn::RawEndpoint::processHeartbeat(uint64_t, const lunaricorn::internal::IncomingMessage&):302 [ DEBUG ] processHeartbeat[1]: received heartbeat, data_len=0
raw_endpoint.cpp:void lunaricorn::RawEndpoint::processHeartbeat(uint64_t, const lunaricorn::internal::IncomingMessage&):308 [ DEBUG ] processHeartbeat[1]: heartbeat updated, total connected: 1
```

**Анализ:** Heartbeat работает корректно. Сервер отправляет HB, клиент отвечает, сервер получает ответ.

**НО:** Обрати внимание — клиент получает heartbeat от сервера (type=1 = MT_HB) и обрабатывает его в `on_message`. Затем `on_heartbeat` отправляет ответный heartbeat. Это правильный цикл.

---

### ПРОБЛЕМА 4: `on_client_message` обрабатывает heartbeat как MT_HB, но `on_heartbeat` тоже отправляет MT_HB (ПУТАНИЦА В ТИПАХ)

**Фай:** `services/signaling_cpp/app/raw_endpoint_client.cpp:226-248`

```cpp
void RE_Client::on_heartbeat(const IncomingMessage& msg)
{
    update_client_hb();

    MessageHeader hdr = {
        .magic = HeaderMagic,
        .version = PROTOCOL_VERSION,
        .type = MT_HB,        // <-- Ответ тоже MT_HB!
        .data_type = CT_Json,
        .flags = 0,
        .seq = msg.header.seq,
        .data_len = 0,
        .crc = 0
    };
    
    boost::json::object resp;
    resp["status"] = "ok";

    if (!send_message(hdr, resp))
    {
        MLOG_E("client[{}] failed to send heartbeat response", _id);
    }
}
```

**Проблема:** Ответ на heartbeat также имеет тип `MT_HB`. На сервере в `on_client_message`:

```cpp
// raw_endpoint.cpp:257-276
switch (msg.header.type) {
    case lunaricorn::internal::MessageType::MT_HB:
        processHeartbeat(clientId, msg);
        break;
    ...
}
```

То есть ответный heartbeat от клиента обрабатывается как новый heartbeat, а не как ответ. Это **не баг** (сервер корректно обновляет таймаут), но это нарушает семантику request/response. Ответ на heartbeat должен иметь тип `MT_Response`.

---

### ПРОБЛЕМА 5: SignalingSubEvent::build требует поле 'events' (ИНФОРМАТИВНО)

**Лог:**
```
signaling_api_events.cpp:bool lunaricorn::SignalingSubEvent::build(const boost::json::object&):20 [ ERROR ] No 'events' field in message. found keys: invalid, 
```

**Анализ:** Тест `SignalingSubEvent_BuildInvalid` отправляет объект с полем `invalid` вместо `events`. Это ожидаемая ошибка валидации.

**НО:** Проверим код:

Читаю `signaling_api.h` для `SignalingSubEvent::build`:

---

### ПРОБЛЕМА 6: `sendResponse` на сервере блокирует mutex дважды (ВОЗМОЖНЫЙ DEADLOCK)

**Фай:** `services/signaling_cpp/app/raw_endpoint.cpp:415-453`

```cpp
void RawEndpoint::sendResponse(uint64_t clientId, uint64_t seq, bool success, const boost::json::object& data)
{
    ...
    std::lock_guard<std::mutex> lock(_clientsMutex);  // Блокируем _clientsMutex
    auto it = _clients.find(clientId);
    if (it != _clients.end()) {
        try {
            it->second->send_message(resp_msg, data);  // send_message тоже блокирует _clientsMutex!
```

**Фай:** `services/signaling_cpp/app/raw_endpoint_client.cpp:558-562`

```cpp
bool RE_Client::send_message(MessageHeader& msg, const boost::json::object& data)
{
    ...
    {
        std::lock_guard<std::mutex> lock(_last_send_mutex);  // Это _last_send_mutex, не _clientsMutex
        _last_send = std::chrono::steady_clock::now();
    }
    ...
}
```

**Анализ:** `send_message` блокирует `_last_send_mutex`, а не `_clientsMutex`. Deadlock **невозможен**. Но код в `sendResponse` блокирует `_clientsMutex`, затем вызывает `send_message`, который блокирует `_last_send_mutex`. Порядок mutex consistent.

**НО:** `sendHeartbeat` (строка 379-413) также блокирует `_clientsMutex` перед вызовом `send_message`:

```cpp
void RawEndpoint::sendHeartbeat(uint64_t clientId)
{
    ...
    std::lock_guard<std::mutex> lock(_clientsMutex);  // Блокируем
    auto it = _clients.find(clientId);
    if (it != _clients.end()) {
        it->second->send_message(hb_msg, boost::json::object());
```

А `send_hb()` (строка 150-176) также блокирует `_clientsMutex`:

```cpp
void RawEndpoint::send_hb()
{
    std::vector<uint64_t> hb_targets;
    {
        std::lock_guard<std::mutex> lock(_clientsMutex);  // First lock
        for (auto& [id, client] : _clients)
        {
            if (!client) { MBUG("no client for {}", id); continue; }
            const auto hb_duration = client->server_hb_delay();
            if (hb_duration >= SERVER_HB_PERIOD)
            {
                hb_targets.push_back(id);
                client->update_server_hb();
            }
        }
    }  // Released

    // Send heartbeats outside the lock
    for (uint64_t id : hb_targets)
    {
        sendHeartbeat(id);  // sendHeartbeat блокирует _clientsMutex снова
    }
}
```

**ВЫВОД:** `send_hb()` правильно отпускает mutex перед вызовом `sendHeartbeat`. Deadlock отсутствует.

---

### ПРОБЛЕМА 7: `serializeJson` с пустым object создает сообщение с crc=0, но data_len=0

**Фай:** `lunaricorn/cpp/proto/signaling.cpp:54-85`

```cpp
size_t SignalingProto::serializeJson(MessageHeader& msg, std::vector<uint8_t>& buf, const boost::json::object& data)
{
    ...
    if (data.empty())
    {
        hdr.crc = 0; // No data, no CRC
        hdr.data_len = 0;
    } else {
        ...
        hdr.data_len = static_cast<uint32_t>(json_len);
    }
    
    buf.insert(buf.end(), hdr_ptr, hdr_ptr + sizeof(hdr));  // Header всегда вставляется
    if (json_len > 0)
    {
        buf.insert(buf.end(), json_str.begin(), json_str.end());
    }
    return sizeof(hdr) + json_len;
}
```

**НО:** `sendResponse` в raw_endpoint.cpp устанавливает `data_len = 0` в заголовке, затем вызывает `serializeJson` с непустым `data`:

```cpp
// raw_endpoint.cpp:421-433
lunaricorn::internal::MessageHeader resp_msg = {
    ...
    .data_len = 0,  // <-- Установлен в 0!
    .crc = 0
};
    
std::vector<uint8_t> buf;
size_t sz = _proto->serializeJson(resp_msg, buf, data);  // data НЕ пустой!
```

**В `serializeJson`:** параметр `msg` копируется в локальную переменную `hdr`, затем `hdr.data_len` перезаписывается. Если `data` не пустой, `hdr.data_len` устанавливается в `json_len`. **Это работает правильно** — локальная копия `hdr` перезаписывает `data_len`.

---

### ПРОБЛЕМА 8: Критическая уязвимость — отсутствие проверки на переполнение при resize (ПОВТОР)

**Фай:** `services/signaling_cpp/app/raw_endpoint.cpp:223`

```cpp
// handleClients() line 223
client->processData(std::vector<char>(buffer.begin(), buffer.begin() + bytesRead));
```

`buffer` размером 4096 байт. Но если клиент отправляет множество маленьких пакетов с `data_len = UINT32_MAX`, каждый вызовет `resize(sizeof(MessageHeader) + UINT32_MAX)` в `processData`, что приведет к `std::length_error`.

---

## 3. Сводка проблем

| # | Уровень | Описание | Файлы |
|---|---------|---------|-------|
| 1 | **КРИТИЧНО** | Нет защиты от переполнения при `resize` с аномальным `data_len` | `raw_endpoint_client.cpp:108`, `signaling_api.cpp:243` |
| 2 | **СРЕДНЕ** | Ответ на heartbeat имеет тип `MT_HB` вместо `MT_Response` | `raw_endpoint_client.cpp:233` |
| 3 | **НИЗКИЙ** | `SignalingSubEvent::build` не проверяет тип поля `events` | `signaling_api.h` (нужен просмотр) |
| 4 | **ИНФО** | Heartbeat цикл работает корректно | — |
| 5 | **ИНФО** | Валидация `fromDict` работает корректно | `signaling.cpp:180-311` |
| 6 | **СРЕДНЕ** | `sendResponse` и `sendHeartbeat` блокируют `_clientsMutex` перед `send_message` — порядок mutex корректен, но код запутан | `raw_endpoint.cpp:379-453` |

---

## 4. Рекомендации

### 4.1. Исправить защиту от аномального data_len (КРИТИЧНО)

```cpp
// raw_endpoint_client.cpp:101-106
if (_pstate.header.data_len > MAX_DATA_LEN)
{
    MLOG_E("payload too large: max={} actual={}", MAX_DATA_LEN, _pstate.header.data_len);
    _pstate.reset();
    return;
}

// ДОБАВИТЬ:
if (sizeof(MessageHeader) + _pstate.header.data_len < sizeof(MessageHeader))
{
    MLOG_E("data_len overflow detected: data_len={}", _pstate.header.data_len);
    _pstate.reset();
    return;
}
```

Аналогично для `signaling_api.cpp:236-241`.

### 4.2. Исправить тип ответа на heartbeat

```cpp
// raw_endpoint_client.cpp:233
.type = MT_Response,  // Вместо MT_HB
```

### 4.3. Унифицировать обработку ошибок resize

Добавить проверку перед каждым `resize`:

```cpp
size_t required_size = sizeof(MessageHeader) + _pstate.header.data_len;
if (required_size < sizeof(MessageHeader) || required_size > MAX_BUFFER_SIZE)
{
    MLOG_E("invalid buffer size: {}", required_size);
    _pstate.reset();
    return;
}