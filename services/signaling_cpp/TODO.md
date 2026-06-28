# TODO: План развития Signaling C++ сервиса

## Цель

Добиться стабильной работы RawEndpoint для управления сообщениями через клиентские классы. Написать тестового C++ клиента. Добавить HTTP endpoint.

---

## Этап 1: Стабильная работа RawEndpoint

### 1.1. Протокол и сериализация

- [ ] Проверить корректность `SignalingProto::serializeJson()`
  - Файл: `proto/signaling.h`
  - Проверить сериализацию всех типов сообщений
  - Проверить расчёт CRC

- [ ] Проверить корректность `SignalingProto::deserializeJson()`
  - Валидация заголовков
  - Обработка ошибок парсинга

- [ ] Убедиться в корректности MessageHeader
  - `magic` — магическое число
  - `version` — версия протокола
  - `type` — тип сообщения
  - `data_type` — тип данных
  - `seq` — последовательный номер
  - `data_len` — длина данных

### 1.2. Клиентский протокол

- [ ] Реализовать `SignalingProto::send_raw()`
  - Отправка заголовка + данных
  - Обработка ошибок отправки

- [ ] Реализовать `SignalingProto::recv_raw()`
  - Получение заголовка + данных
  - Обработка таймаутов

- [ ] Добавить client-side клиентский протокол
  - Файл: `raw_endpoint_client.cpp` (реализация)
  - Метод `connect()` к серверу
  - Метод `disconnect()`
  - Метод `sendHeartbeat()`
  - Метод `sendSubscription()`
  - Метод `sendPushRequest()`
  - Метод `sendQueryRequest()`
  - Метод `receiveResponse()`

### 1.3. Обработка соединений

- [ ] Добавить таймауты для клиентских соединений
  - Если клиент не отправляет heartbeat > 20 сек → закрыть
  - Если сервер не отправляет heartbeat > 20 сек → закрыть

- [ ] Добавить reconnect логику
  - Автоматическое переподключение при разрыве
  - Exponential backoff

- [ ] Добавить health check для клиентов
  - Проверка liveness каждого клиента
  - Логирование долгоживущих соединений

### 1.4. Интеграция с SignalingEngine

- [ ] Связать RawEndpoint с SignalingEngine
  - `RawEndpoint` должен получать `SignalingEngine` как зависимости
  - `processPushRequest()` → `engine->createEvent()`
  - `processQueryRequest()` → `engine->findEvents()`

- [ ] Транслировать события в подписанные клиенты
  - Хранить список подписчиков по типам событий
  - При `handleEvent()` отправлять всем подписанным клиентам

---

## Этап 2: Тестовый C++ клиент

### 2.1. Базовый клиент

- [ ] Создать `test_client/signaling_test_client.cpp`
  - Подключение к серверу
  - Отправка heartbeat
  - Отправка subscription
  - Отправка push request
  - Отправка query request
  - Получение ответов

- [ ] Добавить CLI аргументы
  ```
  signaling_test_client --host <ip> --port <port> --action <hb|sub|push|query|all>
  ```

- [ ] Добавить режим interactive
  ```
  signaling_test_client --interactive
  > hb
  > sub types=event sources=
  > push type=alert data={"key":"value"}
  > query type=alert limit=10
  > exit
  ```

### 2.2. Тестовые сценарии

- [ ] Тест: подключение → heartbeat → отключение

- [ ] Тест: подключение → subscription → публикация → получение

- [ ] Тест: multiple clients simultaneous

- [ ] Тест: reconnect при разрыве соединения

- [ ] Тест: обработка malformed сообщений

### 2.3. Утилиты для тестирования

- [ ] `test_client/CMakeLists.txt` — сборка тестового клиента

- [ ] `it.sh` — скрипт интеграционных тестов
  - Запуск сервера
  - Запуск тестового клиента
  - Проверка результатов

---

## Этап 3: HTTP endpoint

### 3.1. HTTP сервер

- [ ] Добавить HTTP сервер в RawEndpoint
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

- [ ] Добавить конфигурацию для RawEndpoint:
  - `RAW_ENDPOINT_HOST` — IP адрес (по умолч. `0.0.0.0`)
  - `RAW_ENDPOINT_PORT` — порт (по умолч. `5555`)
  - `HTTP_ENDPOINT_HOST` — HTTP IP адрес
  - `HTTP_ENDPOINT_PORT` — HTTP порт (по умолч. `5557`)
  - `HB_INTERVAL` — интервал heartbeat (по умолч. `10`)
  - `CLIENT_TIMEOUT` — таймаут клиента (по умолч. `20`)

- [ ] Загрузка из переменных окружения

### 4.2. Main integration

- [ ] В `main.cpp` запустить RawEndpoint вместо selftest
- [ ] Добавить graceful shutdown
- [ ] Добавить signal handling (SIGTERM, SIGINT)

### 4.3. Docker

- [ ] Обновить `Dockerfile` для нового порта
- [ ] Обновить `docker-compose.yaml`
- [ ] Добавить healthcheck для C++ сервиса

---

## Этап 5: Тестирование и документация

- [ ] Unit тесты для SignalingProto
- [ ] Unit тесты для MessageStorage
- [ ] Unit тесты для RawEndpoint protocol handling
- [ ] Интеграционные тесты через `it.sh`
- [ ] Обновить `doc/ARCHITECTURE.md`
- [ ] Обновить `doc/STATUS.md`
- [ ] Написать `README.md` для signaling_cpp

---

## Приоритеты

| Приоритет | Этапы |
|-----------|-------|
| Critical | Этап 1 (RawEndpoint стабильность) |
| High | Этап 2 (тестовый клиент) |
| Medium | Этап 3 (HTTP endpoint) |
| Low | Этап 4-5 (конфигурация, документация) |

---

## Зависимости между задачами

```
Этап 1: Стабильный RawEndpoint
├── Протокол (proto/signaling.h)
│   └──→ send_raw() реализация
│   └──→ recv_raw() реализация
├── Клиентский протокол (raw_endpoint_client)
│   └──→ connect()
│   └──→ sendHeartbeat()
│   └──→ sendSubscription()
│   └──→ sendPushRequest()
│   └──→ sendQueryRequest()
├── Интеграция с SignalingEngine
│   └──→ processPushRequest → engine->createEvent()
│   └──→ processQueryRequest → engine->findEvents()
│   └──→ handleEvent → broadcast to subscribers
└── Таймауты и reconnect
    └──→ client health check
    └──→ reconnect logic

Этап 2: Тестовый клиент
├── signaling_test_client.cpp
│   └──→ CLI аргументы
│   └──→ interactive режим
├── Тестовые сценарии
└── CMakeLists.txt + it.sh

Этап 3: HTTP endpoint
├── Poco HTTP Server
├── Маршруты (/health, /status, /events, ...)
└── Интеграция с SignalingEngine

Этап 4-5: Конфигурация и документация
```

---

## Оценка сложности

| Задача | Сложность | Время (ч) |
|--------|-----------|-----------|
| Протокол и сериализация | Средняя | 4-6 |
| Клиентский протокол | Средняя | 6-8 |
| Обработка соединений | Средняя | 4-6 |
| Интеграция с Engine | Средняя | 4-6 |
| Тестовый клиент | Средняя | 6-8 |
| HTTP endpoint | Средняя | 8-12 |
| Конфигурация и запуск | Низкая | 2-4 |
| Тестирование | Средняя | 6-8 |
| **Итого** | | **40-58** |

---

*Создано: 28.06.2026*