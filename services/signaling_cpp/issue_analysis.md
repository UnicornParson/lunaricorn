# Анализ проблем `services/signaling_cpp/issue.md`

## Контекст

CLI (`main.cpp`) создаёт **два независимых** `SignalingConnector`:
1. `SignalingConnector connector` (строка 214) — для получения событий и ответов
2. `TestSender test_sender` (строка 238), который внутри создаёт `m_connector = std::make_shared<lunaricorn::SignalingConnector>()` (test_sender.cpp:78) — для отправки push-событий

Оба коннектора подключаются к одному серверу `127.0.0.1:8080`. Это **намеренный сценарий** — система должна поддерживать любое количество параллельных коннекторов.

---

## Проблема 1: `receiveBytes` возвращает -1 (EAGAIN) → ложный close соединения

### Симптом (серверные логи)

```
raw_endpoint.cpp:receiveBytes failed for client#1: error code=-1
raw_endpoint.cpp:on_client_closed[1]: client closing, current connected: 2
```

### Причина

В `raw_endpoint.cpp:handleClients()` (строка 216):

```cpp
int bytesRead = client->socket().receiveBytes(buffer.data(), static_cast<int>(buffer.size()));
...
else if (bytesRead < 0)
{
    MLOG_E("receiveBytes failed for client#{}: error code={}", id, bytesRead);
    on_client_closed(id);
    break;
}
```

Сокет клиента установлен в неблокирующий режим (raw_endpoint.cpp:115):
```cpp
client->socket().setBlocking(false);
```

В неблокирующем режиме `receiveBytes()` возвращает **-1** с `errno = EAGAIN/EWOULDBBLOCK`, когда данных нет — это **нормальное состояние**, а не ошибка. Код обработки должен пропустить клиента и продолжить цикл, а не закрывать соединение.

**Это главная причина дисконнекта client#1.** Клиент не отключился — сервер ошибочно закрыл соединение из-за неправильной обработки EAGAIN.

---

## Проблема 2: Race condition — `_connected` установлен до запуска runner()

### Симптом (клиентские логи)

```
[2026-07-02T20:05:58.855Z] Connected!
[2026-07-02T20:06:08.858Z] RESP | type=RESP   ← 10 секунд задержки!
```

### Причина

В `signaling_api.cpp:start()` (строки 74-81):

```cpp
_runner_thread = std::jthread([this](std::stop_token st){runner(st, _thread_state);});
...
_hb_timer.start(...);
_connected = true;  // ← ПОСЛЕ запуска потока и таймера
```

`_connected = true` устанавливается **после** запуска таймера heartbeat, но **до** того как `runner()` начнёт `poll()` на сокете.

`onHBTimer()` (строка 143):
```cpp
void SignalingConnector::onHBTimer(Poco::Timer&)
{
    if (!ready()) {return;}
    ...
    send_client_hb();
}
```

`ready()` (строка 131):
```cpp
bool SignalingConnector::ready()
{
    return _connected && _sock && _sock->impl()->initialized();
}
```

Поскольку `_connected = true` уже установлено, `ready()` возвращает `true`, и heartbeat отправляется **до того**, как `runner()` начал читать данные. Если сервер отвечает на heartbeat эхом, эти данные остаются в буфере сокета непрочитанными — `runner()` ещё не начал `poll()`.

Когда `runner()` наконец начинает `poll()`, данные уже пришли, но TCP-состояние может быть нарушено (зависит от таймингов).

**Фикс:** Установить `_connected = true` **после** того как `runner()` подтвердит готовность, или перенести `_connected = true` **перед** запуском потока но **перед** стартом таймера:

```cpp
_connected = true;  // ← ПЕРЕД стартом таймера
_hb_timer.start(...);
```

Но лучше — проверять состояние сокета в `ready()` вместо флага `_connected`.

---

## Проблема 3: 10-секундная задержка обработки ответа сервера

### Симптом

Подписка отправлена сразу, ответ получен через 10 секунд.

### Причина

В `signaling_api.cpp:runner()` (строка 411):
```cpp
static const Poco::Timespan pollTimeout(1000000); // 1 000 000 микросекунд = 1 секунда
```

`poll()` с таймаутом 1 секунда. Ответ сервера на подписку приходит в TCP-буфере, но клиент опрашивает сокет раз в секунду.

Задержка ~10 секунд объясняется тем, что:
1. Подписка отправлена
2. Сервер мгновенно отвечает
3. Но `runner()` находится в `poll()` с таймаутом 1с
4. Плюс таймер heartbeat срабатывает через ~10 секунд (зависит от момента старта)
5. Heartbeat триггерит дополнительную обработку

**Это не баг, а особенность polling-реализации.** Для production следует использовать `Poco::AsyncSocket` или `Poco::Net::HTTPServerConnection` с non-blocking I/O.

---

## Проблема 4: Pending-response race condition

### Симптом (клиентские логи)

```
signaling_api.cpp:on_message.320 MessageHeader: type=2 data_len=37
signaling_api.cpp:on_server_request.389 on server response (not matched via pending)
```

Ответ `MT_Response` (type=2) не находится в `_pending_responses`.

### Причина

В `signaling_api.cpp:subscribe()` (строки 592-597):

```cpp
_pending_responses[seq] = resp;  // ← Сначала добавляем в pending
...
MLOG_D("send subscribe seq={}, types={}", seq, types.size());
return send_message(header, sub_data);  // ← Потом отправляем
```

Кажется правильным, НО! Сервер отправляет **два** ответа на подписку:

1. `sendResponse[1]` с seq=0 (raw_endpoint.cpp:73) — ответ на подписку
2. `sendResponse[1]` с seq=0 (raw_endpoint.cpp:74) — дубликат!

Посмотрев `processSubscription()` (raw_endpoint.cpp:325-341):
```cpp
MLOG_D("processSubscription[1]: sending acknowledgment");
sendResponse(clientId, msg.header.seq, true);  // ← Один ответ
MLOG_D("processSubscription[1]: subscription acknowledged");
```

Сервер отправляет один ответ. Но в логах сервера:
```
Line 73: sendResponse[1]: sending response, seq=0, success=true, data_obj_size=0
Line 74: send_message ... type=2 data_len=0
Line 75: try to send 24b
Line 76: sendResponse[1]: response sent successfully
```

Это **один** ответ. Но на клиенте приходит **два** сообщения type=2:
```
Line 28: type=2 data_len=37 → not matched via pending
Line 30: type=2 data_len=0 → not matched via pending
```

Первое (data_len=37) — это, вероятно, header + payload ответа на подписку.
Второе (data_len=0) — это, вероятно, heartbeat echo (type=1), но сбитый заголовок.

**Главная причина:** `_pending_responses` используется без защиты от race condition между потоками. `subscribe()` кладёт entry в map из основного потока, а `runner()` читает из потока чтения. Хотя есть `_pending_responses_mutex`, порядок операций может нарушиться при высокой конкуренции.

**Фикс:** Убедиться, что `_pending_responses[seq] = resp` происходит **перед** `send_message()` и что `send_message()` не вызывает асинхронный обратный вызов, который может очистить pending.

---

## Проблема 5: Broken pipe при отправке ответа

### Симптом (серверные логи)

```
raw_endpoint_client.cpp [ERROR] send_bytes exception: I/O error: Broken pipe
```

### Причина

Следствие Проблемы 1. Сервер пытается отправить ответ на push-запрос client#2, но сокет уже закрыт (клиент дисконнектился из-за проблемы 1).

**Исчезнет вместе с фиксом Проблемы 1.**

---

## Проблема 6: Сервер создаёт client#2 при живом client#1

### Симптом (серверные логи)

```
Line 53: acceptLoop: new connection from 127.0.0.1:127.0.0.1, port 8080  ← client#1
Line 62: acceptLoop: new connection from 127.0.0.1:127.0.0.1, port 8080  ← client#2
```

### Анализ

Это **нормальное поведение** для сервера — он должен принимать любое количество подключений. Проблема в том, что оба коннектора от одного процесса (одного PID) могут иметь конфликты на уровне ОС, если они используют одни и те же локальные порт/сокет.

На сервере **нет** логики дедупликации клиентов — каждый `accept()` создаёт нового `RE_Client`. Это правильно для multi-connector сценария.

---

## Сводная таблица

| # | Проблема | Severity | Статус |
|---|----------|----------|--------|
| 1 | EAGAIN (-1) обрабатывается как ошибка → ложный close | **CRITICAL** | Фикс: проверить errno == EAGAIN/EWOULDBLOCK |
| 2 | Race condition: `_connected` до запуска runner() | **HIGH** | Фикс: порядок установки флагов |
| 3 | 10-сек задержка ответа | MEDIUM | Фикс: async I/O или shorter poll timeout |
| 4 | Pending-response не находится | HIGH | Фикс: атомарность pending + send |
| 5 | Broken pipe | LOW | Исчечнет с фиксом #1 |
| 6 | Двойное подключение | INFO | Нормальное поведение |

---

## Рекомендации по исправлению

### 1. Fix EAGAIN handling (raw_endpoint.cpp:handleClients, строка 231)

```cpp
else
{
    // bytesRead < 0: non-blocking read, check errno
    int err = Poco::Socket::getErrorCode();
    if (err == EAGAIN || err == EWOULDBLOCK)
    {
        // No data available, skip to next client
        continue;
    }
    MLOG_E("receiveBytes failed for client#{}: error code={}", id, bytesRead);
    on_client_closed(id);
    break;
}
```

Или корректно получить код ошибки:
```cpp
else
{
    int errorCode = Poco::Net::checkSocketError(id);
    if (errorCode == POCO_ERR_EAGAIN || errorCode == POCO_ERR_EWOULDBLOCK)
        continue;
    MLOG_E("receiveBytes failed for client#{}: error code={}", id, bytesRead);
    on_client_closed(id);
    break;
}
```

### 2. Fix connection state ordering (signaling_api.cpp:start)

```cpp
// Установить _connected ПЕРЕД стартом таймера
_connected = true;
_hb_timer.start(Poco::TimerCallback<SignalingConnector>(*this, &SignalingConnector::onHBTimer));
```

### 3. Fix poll timeout (signaling_api.cpp:runner)

```cpp
// Уменьшить poll timeout с 1s до 100ms для более быстрой реакции
static const Poco::Timespan pollTimeout(100000); // 100ms
```

### 4. Server: add client dedup logging

Добавить в `acceptLoop()` логирование ID нового клиента для отладки multi-connector сценария.