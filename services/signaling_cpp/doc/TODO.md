# TODO: Чеклист готовности Signaling C++ сервиса


## Цель

Список задач, выполнение которых означает, что сервис **готов к использованию** как надёжная основа для разработки других сервисов. После завершения всего списка можно переходить к следующему микросервису.

---

## Статус компонентов (текущее состояние)

| Компонент | Статус | Примечание |
|-----------|--------|------------|
| RawEndpoint (acceptLoop + handleClients) | ✅ Работает | TCP-сервер на Poco::Socket |
| RE_Client (парсинг протокола, heartbeat) | ✅ Работает | Инкрементальный парсинг IncomingPacketState |
| SignalingProto (serializeJson/deserializeJson/send_raw) | ✅ Работает | CRC, header validation |
| SignalingEngine (CRUD, подписки, фильтрация) | ✅ Работает | createEvent, findEvents, subscribe, dispatch |
| MessageStorage (PostgreSQL через soci) | ✅ Работает | Таблица signaling_events |
| Telemetry (сбор статистики) | ✅ Работает | Singleton, push/error/active метрики, toJson |
| HTTP endpoint (Boost.Beast) | ✅ Работает | Маршруты: / /health /push /pull /stat |
| CLI клиент + HttpTest | ✅ Работает | cli/main.cpp, TestSender, HttpTest |
| main.cpp (запуск + graceful shutdown) | ✅ Работает | RawEndpoint + HttpServer + SignalWaiter |
| Docker (образы + compose) | ✅ Есть | Dockerfile.*, docker-compose.yaml |
| processSubscription (подписки) | ✅ Работает | Парсинг фильтров + engine->subscribe() |
| processPushRequest (публикация) | ✅ Работает | createEvent + dispatchEvent + sendEventToClient |
| processQueryRequest (запрос событий) | ⚠️ Заглушка | Отправляет ACK, не использует engine->findEvents() |
| Reconnect (автопереподключение) | ✅ Реализовано | Exponential backoff (1s–60s) + jitter + restore subscriptions |

---

## Чеклист готовности к продакшену

### 🔴 Критическое (блокирует переход к следующему сервису)

- [ ] **Реализовать processQueryRequest**
  - Связать с `SignalingEngine::findEvents()` / `findEventsByType()`
  - Парсить query-параметры из payload (offset, limit, type, source)
  - Отправлять результаты клиенту
  - Файл: `app/raw_endpoint.cpp::processQueryRequest()`

- [x] **Добавить reconnect логику для клиентов**
  - Exponential backoff (1s–60s) + full jitter (ReconnectStrategy)
  - Автоматическое переподключение SignalingConnector при разрыве
  - Восстановление подписок после переподключения (SubscriptionCache)
  - Файлы: `lunaricorn/cpp/signaling_reconnect.h`, `lunaricorn/cpp/signaling_api.h/cpp`
  - CLI: `cli/main.cpp` — `connector.set_auto_reconnect(true)`


### 🟡 Высокое (значительно повышает надёжность)

- [ ] **Docker healthcheck**
  - Добавить `HEALTHCHECK` в Dockerfile через curl к `GET /health` (порт 8081)
  - Обновить `docker-compose.yaml` с healthcheck

- [ ] **Создать README.md**
  - Prerequisites (C++20, CMake, Boost, Poco, soci, PostgreSQL)
  - Build (cmake, make)
  - Configuration (переменные окружения: DB_HOST, DB_PORT, DB_NAME, DB_USER, DB_PASSWORD)
  - Run (локально, docker)
  - Testing (CLI клиент, HTTP endpoint)

### 🟢 Среднее (желательно, но не блокирует)

- [ ] **Обновить STATUS.md**
  - Актуализировать раздел по signaling_cpp в корневом `doc/STATUS.md`

- [ ] **Telemetry: опциональное логирование в файл**
  - Добавить конфигурацию для записи метрик в файл в дополнение к MLOG_D

### 🔵 Низкое (можно отложить)

- [ ] **Удаление мёртвого кода**

---

## График зависимостей

```
Критическое (блокирует переход)
└── [ ] processQueryRequest — 2-3ч

Высокое
├── [ ] Docker healthcheck — 1ч
└── [ ] README.md — 1ч

Среднее
├── [ ] STATUS.md обновление — 0.5ч
└── [ ] Telemetry file logging — 1ч

Низкое
└── [ ] Удаление мёртвого кода — 0.5ч

Оценка: ~5-6 часов до полной готовности
```

---

## Критерии готовности "можно положиться на сервис"

1. ✅ RawEndpoint стабильно принимает соединения и обрабатывает heartbeat
2. ✅ HTTP endpoint отвечает на /health, /push, /stat
3. ✅ Подписки работают: client → subscribe → push → dispatch → subscriber получает событие
4. ⚠️ **processQueryRequest должен быть реализован** (заглушка)
5. ✅ **Reconnect реализован** (exponential backoff, auto-reconnect, restore subscriptions)

Пункт 4 — минимальный набор для перехода к разработке следующего сервиса.

---

## Завершённые этапы (история)

### Этап 1: RawEndpoint (✅ ЗАВЕРШЁН)
- Протокол и сериализация
- Клиентский протокол (RE_Client)
- Обработка соединений (heartbeat, таймауты)
- Интеграция с SignalingEngine (подписки, push, dispatch)

### Этап 2: CLI + HTTP клиенты (✅ ЗАВЕРШЁН)
- cli/main.cpp + TestSender + HttpTest
- Запуск и graceful shutdown

### Этап 3: HTTP endpoint (✅ ЗАВЕРШЁН)
- Полноценный HttpServer на Boost.Beast
- REST API: GET /, /health, /push, /pull, /stat
- Интеграция с SignalingEngine и Telemetry

### Этап 4: Docker (✅ ЗАВЕРШЁН)
- Dockerfile.base, Dockerfile.builder, Dockerfile.tester, Dockerfile
- docker-compose.yaml

### Этап 5: Telemetry (✅ ЗАВЕРШЁН)
- Глобальный singleton Telemetry
- recordPushSuccess, recordError, setActiveClients
- toJson(), printReport()

---

*Создано: 28.06.2026*
*Обновлено: 04.07.2026 — убраны пункты об автотестах, сервис тестируется через CLI*
