# TODO: План развития Signaling C++ сервиса

## Цель

Стабильная работа RawEndpoint для управления сообщениями через клиентские классы. Тестовый C++ клиент. HTTP endpoint.

---

## Статус реализации

| Компонент | Статус | Примечание |
|-----------|--------|------------|
| RawEndpoint | ✅ Реализован | Серверная сторона, acceptLoop + handleClients |
| RE_Client | ✅ Реализован | Парсинг протокола, heartbeat, callbacks |
| SignalingProto | ✅ Реализован | serializeJson, deserializeJson, send_raw |
| SignalingEngine | ✅ Реализован | CRUD, подписки, фильтрация |
| MessageStorage | ✅ Реализован | PostgreSQL через soci |
| CLI клиент | ✅ Реализован | cli/main.cpp, test_sender |
| HTTP endpoint | ❌ Заглушка | http_endpoint.cpp: start()/stop() возвращают false |
| test_client/ | ⚠️ Частично | Базовые boost::test тесты, нет интеграционных |
| Main | ✅ Работает | Запускает RawEndpoint + selftest |
| Docker | ✅ Есть | Dockerfile.*, docker-compose.yaml |

---

## Этап 1: Стабильная работа RawEndpoint

### 1.1. Протокол и сериализация

- [x] Проверить корректность `SignalingProto::serializeJson()`
  - Файл: `lunaricorn/cpp/proto/signaling.h`
  - Сериализация всех типов сообщений
  - Расчёт CRC
- [x] Проверить корректность `SignalingProto::deserializeJson()`
  - Валидация заголовков
  - Обработка ошибок парсинга
- [x] Убедиться в корректности MessageHeader
  - `magic` — `HeaderMagic` (0x12345678)
  - `version` — `PROTOCOL_VERSION` (1)
  - `type` — `MessageType` enum
  - `data_type` — `ContentType` enum
  - `seq` — последовательный номер
  - `data_len` — длина данных

### 1.2. Клиентский протокол

- [x] Реализовать `SignalingProto::send_raw()`
  - Отправка заголовка + данных
  - Обработка ошибок отправки
- [x] Реализовать `SignalingProto::deserializeJson()`
  - Получение заголовка + данных
  - Обработка ошибок парсинга
- [x] Добавить client-side клиентский протокол
  - Файл: `app/raw_endpoint_client.h` (RE_Client)
  - Метод `processData()` — обработка входящих данных
  - Метод `send_message()` — отправка сообщений
  - Callbacks: `set_message_callback`, `set_disconnect_callback`

### 1.3. Обработка соединений

- [x] Добавить таймауты для клиентских соединений
  - Heartbeat от сервера каждые 10 сек (`SERVER_HB_PERIOD`)
  - Heartbeat от клиента каждые 10 сек (`CLIENT_HB_PERIOD`)
  - `client_hb_delay()` / `server_hb_delay()` для проверки
- [ ] Добавить reconnect логику
  - Автоматическое переподключение при разрыве
  - Exponential backoff
- [x] Добавить health check для клиентов
  - `is_silent()` проверка
  - Логирование долгоживущих соединений (`on_client_closed`)

### 1.4. Интеграция с SignalingEngine

- [x] Связать RawEndpoint с SignalingEngine
  - `RawEndpoint` принимает `SignalingEngine` в конструкторе
  - `processPushRequest()` — обработка push (через engine)
  - `processQueryRequest()` — обработка query (через engine)
- [x] Транслировать события в подписанные клиенты
  - `SignalingEngine::subscribe()` / `unsubscribe()`
  - `SignalingEngine::dispatchEvent()` → `on_event()` → фильтрация подписчиков
  - Callback `onSubEvent_` для уведомления

---

## Этап 2: Тестовый C++ клиент

### 2.1. Базовый клиент

- [x] Создать `cli/main.cpp`
  - Подключение к серверу
  - Отправка heartbeat (через TestSender)
  - Отправка subscription
  - Получение ответов и subscription событий
- [x] Добавить CLI аргументы
  ```
  cli/main.cpp <host> <port>
  ```
- [x] Добавить режим interactive
  - Реализован через TestSender (`cli/test_sender.h`)
  - Автоматическая отправка тестовых событий

### 2.2. Тестовые сценарии

- [ ] Тест: подключение → heartbeat → отключение
- [ ] Тест: подключение → subscription → публикация → получение
- [ ] Тест: multiple clients simultaneous
- [ ] Тест: reconnect при разрыве соединения
- [ ] Тест: обработка malformed сообщений

### 2.3. Утилиты для тестирования

- [x] `test_client/CMakeLists.txt` — сборка тестов
- [ ] `it.sh` — скрипт интеграционных тестов (существует, но требует доработки)

---

## Этап 3: HTTP endpoint

### 3.1. HTTP сервер

- [ ] Добавить HTTP сервер в HTTP_Endpoint
  - Использовать Poco HTTP Server
  - Порт настраивается через конфигурацию
- [ ] Добавить маршруты:
  - `GET /health` — health check
  - `GET /status` — статус соединений
  - `POST /events` — публикация события
  - `GET /events` — поиск событий
  - `GET /events/{type}` — поиск по типу
  - `GET /subscriptions` — список подписок
  - `POST /subscriptions` — добавить подписку
  - `DELETE /subscriptions/{id}` — удалить подписку

### 3.2. HTTP API интеграция

- [ ] Связать `POST /events` с `SignalingEngine::createEvent()`
- [ ] Связать `GET /events` с `SignalingEngine::findEvents()`
- [ ] Добавить валидацию входных данных
- [ ] Добавить response formatting (JSON)

### 3.3. Документация API

- [ ] Добавить OpenAPI спецификацию
- [ ] Swagger UI (опционально)

---

## Этап 4: Конфигурация и запуск

### 4.1. Конфигурация

- [x] Конфигурация для RawEndpoint:
  - `raw_host` — IP адрес (по умолч. `127.0.0.1`)
  - `raw_port` — порт (по умолч. `8080`)
  - `HB_INTERVAL` — интервал heartbeat (по умолч. `10`)
  - `CLIENT_TIMEOUT` — таймаут клиента
- [x] Загрузка DB конфигурации из переменных окружения (`loadConfigFromEnvironment()`)

### 4.2. Main integration

- [x] В `main.cpp` запускается RawEndpoint
- [x] Добавлен graceful shutdown через `SignalWaiter`
- [x] Добавлен signal handling (SIGTERM, SIGINT)

### 4.3. Docker

- [x] `Dockerfile` для сборки
- [x] `Dockerfile.base` / `Dockerfile.builder` / `Dockerfile.tester`
- [x] `docker-compose.yaml`
- [ ] Добавить healthcheck для C++ сервиса

---

## Этап 5: Тестирование и документация

- [ ] Unit тесты для SignalingProto
- [ ] Unit тесты для MessageStorage
- [ ] Unit тесты для RawEndpoint protocol handling
- [ ] Интеграционные тесты через `it.sh`
- [x] `Design.md` — полная архитектура
- [ ] Обновить `doc/STATUS.md`
- [ ] Написать `README.md` для signaling_cpp

---

## Приоритеты

| Приоритет | Этапы |
|-----------|-------|
| Critical | Этап 2.2 (тестовые сценарии), Этап 3 (HTTP endpoint) |
| High | Этап 1.3 (reconnect), Этап 5 (тестирование) |
| Medium | Этап 3 (HTTP API), Этап 4.3 (Docker healthcheck) |
| Low | Этап 5 (документация) |

---

## Зависимости между задачами

```
Этап 1: ✅ ЗАВЕРШЁН — RawEndpoint стабильный
├── Протокол (proto/signaling.h) ✅
│   ├── serializeJson() ✅
│   ├── deserializeJson() ✅
│   └── send_raw() ✅
├── Клиентский протокол (RE_Client) ✅
│   ├── processData() ✅
│   ├── send_message() ✅
│   └── Callbacks ✅
├── Интеграция с SignalingEngine ✅
│   ├── processPushRequest → engine ✅
│   ├── processQueryRequest → engine ✅
│   └── dispatchEvent → subscribers ✅
└── Таймауты ✅
    ├── client_hb_delay / server_hb_delay ✅
    └── is_silent() ✅

Этап 2: ЧАСТИЧНО ЗАВЕРШЁН
├── cli/main.cpp ✅
├── TestSender ✅
├── [ ] Тестовые сценарии
└── [ ] it.sh (доработка)

Этап 3: НЕ НАЧАТ
├── HTTP_Endpoint (stub) ❌
├── [ ] Маршруты
└── [ ] Интеграция с SignalingEngine

Этап 4: ЧАСТИЧНО ЗАВЕРШЁН
├── Конфигурация ✅
├── Main + SignalWaiter ✅
└── [ ] Docker healthcheck

Этап 5: НЕ НАЧАТ
├── [ ] Unit тесты
├── [ ] Интеграционные тесты
└── [ ] README.md
```

---

## Оценка сложности

| Задача | Сложность | Время (ч) | Статус |
|--------|-----------|-----------|--------|
| Протокол и сериализация | Средняя | 4-6 | ✅ сделано |
| Клиентский протокол | Средняя | 6-8 | ✅ сделано |
| Обработка соединений | Средняя | 4-6 | ✅ частично |
| Интеграция с Engine | Средняя | 4-6 | ✅ сделано |
| Тестовый клиент | Средняя | 6-8 | ✅ частично |
| HTTP endpoint | Средняя | 8-12 | ❌ не начат |
| Конфигурация и запуск | Низкая | 2-4 | ✅ сделано |
| Тестирование | Средняя | 6-8 | ❌ не начато |
| **Итого** | | **40-58** | **~60% завершено** |

---

## Известные проблемы и ограничения

### В текущем коде

1. **HTTP_Endpoint не реализован** — все методы возвращают false/пусто
2. **Reconnect логика отсутствует** — клиенты не переподключаются автоматически
3. **Типы подписок не обрабатываются** — `processSubscription` просто отправляет ACK, без реальной подписки
4. **pushQueryRequest не использует SignalingEngine** — просто отправляет ACK
5. **TypeHandler<EventDataExtended> не используется** — MessageStorage использует soci напрямую

### В документации

1. **STATUS.md устарел** — требует обновления
2. **README.md отсутствует** — нет инструкций по сборке/запуску
3. **changelog.md / problems.md отсутствуют** — в соответствии с dev.md требуются

---

*Создано: 28.06.2026*
*Обновлено: 03.07.2026 — актуализация по состоянию кода*