# TODO: Чеклист готовности Signaling C++ сервиса для кластера Lunaricorn

## Цель

Список задач для подготовки signaling_cpp к использованию как **основной сервис signaling** в кластере Lunaricorn, вместо текущего Python-реализования.

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
| HTTP endpoint (Boost.Beast) | ✅ Работает | Маршруты: / /health /v1/list/* /v1/stat/clients /push /pull /stat |
| CLI клиент + HttpTest | ✅ Работает | cli/main.cpp, TestSender, HttpTest с ✅/❌ выводом |
| main.cpp (запуск + graceful shutdown) | ✅ Работает | RawEndpoint + HttpServer + SignalWaiter |
| Docker (образы + compose) | ✅ Есть | Dockerfile.*, docker-compose.yaml |
| processSubscription (подписки) | ✅ Работает | Парсинг фильтров + engine->subscribe() |
| processPushRequest (публикация) | ✅ Работает | createEvent + dispatchEvent + sendEventToClient |
| processQueryRequest (запрос событий) | ✅ Работает | Парсит параметры, вызывает engine->findEvents(), отправляет результаты |
| Reconnect (автопереподключение) | ✅ Реализовано | Exponential backoff (1s–60s) + jitter + restore subscriptions |
| GET /v1/list/* (совместимое API) | ✅ Реализовано | tags, types, affected, owners — engine->getUniqueValues() |
| GET /v1/stat/clients | ✅ Реализовано | Телеметрия + активные клиенты |

---

## Анализ совместимости с Python signaling API

### Текущий Python API (port 5557)

| Endpoint | Метод | Назначение |
|----------|-------|------------|
| `/` | GET | Статус сервиса |
| `/health` | GET | Health check |
| `/v1/list/tags` | GET | Уникальные теги |
| `/v1/list/types` | GET | Уникальные типы событий |
| `/v1/list/affected` | GET | Уникальные affected значения |
| `/v1/list/owners` | GET | Уникальные источники |
| `/v1/stat/clients` | GET | Список активных клиентов |
| `/v1/browse` | POST | Поиск событий (BrowseRequest) |

### Текущий C++ HTTP API (port 8081)

| Endpoint | Метод | Назначение |
|----------|-------|------------|
| `/` | GET | Статус сервиса |
| `/health` | GET | Health check |
| `/push` | POST | Публикация события |
| `/pull?offset=N` | GET | Запрос событий |
| `/stat` | GET | Телеметрия |

### Различия, требующие устранения

1. **Порт**: Python использует `5557`, C++ использует `8081` — нужно синхронизировать
2. **Префикс API**: Python использует `/v1/*`, C++ использует без префикса — клиенты orb уже используют `/push`/`/pull` без префикса
3. **Отсутствующие эндпоинты в C++**:
   - ~~`GET /v1/list/tags` — уникальные теги~~ ✅ Реализовано
   - ~~`GET /v1/list/types` — уникальные типы событий~~ ✅ Реализовано
   - ~~`GET /v1/list/affected` — уникальные affected значения~~ ✅ Реализовано
   - ~~`GET /v1/list/owners` — уникальные источники~~ ✅ Реализовано
   - ~~`GET /v1/stat/clients` — список активных клиентов~~ ✅ Реализовано
   - `POST /v1/browse` — поиск событий с фильтрами
4. **Формат POST /push**:  - больше не будет поддерживаться.
   - Python: `{"type": "push", "event_type": "...", "message": {...}}` (через ZMQ)
   - C++ HTTP: `{"type": "...", "source": "...", "affected": [...], "tags": [...], "payload": {...}}`
   - C++ формат совместим с orb (использует тот же формат)
5. **POST /pull vs POST /v1/browse**:
   - Python: `POST /v1/browse` с BrowseRequest body
   - C++: `GET /pull?offset=N` — только offset, без фильтров

---

## Задачи по включению в кластер

### 🔴 Критическое (блокирует замену Python signaling)

- [ ] **Добавить LeaderConnector интеграцию**
  - Подключить LeaderConnector из `lunaricorn/cpp/leader_api.h` в main.cpp
  - Добавить регистрацию сервиса через `register_service()` с node_type = "signaling"
  - Добавить поддержку переменной окружения `CLUSTER_LEADER_URL`
  - Добавить `wait_for_ready()` при старте
  - Файл: `app/main.cpp`, `lunaricorn/cpp/leader_api.h`

- [x] **Добавить отсутствующие HTTP эндпоинты**
  - ~~`GET /v1/list/tags` → engine->getUniqueValues("tags")~~ ✅
  - ~~`GET /v1/list/types` → engine->getUniqueValues("type")~~ ✅
  - ~~`GET /v1/list/affected` → engine->getUniqueValues("affected")~~ ✅
  - ~~`GET /v1/list/owners` → engine->getUniqueValues("owner")~~ ✅
  - ~~`GET /v1/stat/clients` → Telemetry::instance().activeClients()~~ ✅
  - `POST /v1/browse` → engine->findEvents() с парсингом BrowseRequest body
  - Файл: `app/http_endpoint.cpp`

- [x] **Синхронизировать порт HTTP API** - порт определяется в композ файле. внутренний порт не важен
  - Изменить порт с `8081` на `5557` (совместимость с Python API)
  - Или добавить переменную окружения `SIGNALING_API_PORT`
  - Файл: `app/main.cpp`

- [ ] **Добавить cluster-aware конфигурацию**
  - Добавить переменные окружения: `CLUSTER_LEADER_URL`, `SIGNALING_API_PORT`, `SIGNALING_HTTP_PORT`
  - Считать host из discovery (через LeaderConnector) для передачи в лидер
  - Файл: `app/main.cpp`

### 🟡 Высокое (значительно повышает надёжность)

- [ ] **Docker healthcheck**
  - Добавить `HEALTHCHECK` в Dockerfile через curl к `GET /health`
  - Обновить `docker-compose.yaml` с healthcheck для signaling_cpp
  - Файл: `services/signaling_cpp/Dockerfile`, `services/docker-compose.yaml`

- [ ] **Создать README.md**
  - Prerequisites (C++20, CMake, Boost, Poco, soci, PostgreSQL)
  - Build (cmake, make)
  - Configuration (переменные окружения)
  - Run (локально, docker)
  - Testing (CLI клиент, HTTP endpoint)
  - Cluster integration (CLUSTER_LEADER_URL, регистрация в лидере)
  - Файл: `services/signaling_cpp/README.md`

- [ ] **Заменить Python signaling в docker-compose.yaml**
  - Создать сервис `signaling_cpp` с портами 5555, 5556, 5557
  - Добавить зависимости: pg, leader, maintenance
  - Настроить environment: CLUSTER_LEADER_URL, DB_*
  - Обновить сервис `orb` для использования нового SIGNALING_API
  - Файл: `services/docker-compose.yaml`

- [ ] **Реализовать GET /pull с фильтрами (аналог /v1/browse)**
  - Парсить query parameters: types, sources, affected, tags, timestamp, limit
  - Вызывать engine->findEvents() с фильтрами
  - Отправлять результаты клиенту
  - Файл: `app/http_endpoint.cpp::handle_pull()`

### 🟢 Среднее (желательно, но не блокирует)

- [ ] **Обновить STATUS.md**
  - Актуализировать раздел по signaling_cpp в корневом `doc/STATUS.md`

- [ ] **Миграция клиентов с ZMQ на HTTP**
  - orb использует SIGNALING_REQ/SIGNALING_PUB (ZMQ) — проверить возможность HTTP
  - Обновить все сервисы, использующие старый ZMQ signaling

- [ ] **Telemetry: опциональное логирование в файл**
  - Добавить конфигурацию для записи метрик в файл

### 🔵 Низкое (можно отложить)

- [ ] **Удаление мёртвого кода**
  - Проверить и удалить неиспользуемые файлы/классы

---

## График зависимостей

```
Критическое (блокирует переход)
├── [ ] LeaderConnector интеграция — 2ч
├── [ ] Добавить отсутствующие HTTP эндпоинты — 3ч
├── [ ] Синхронизировать порт HTTP API — 0.5ч
└── [ ] Cluster-aware конфигурация — 1ч

Высокое
├── [ ] Docker healthcheck — 1ч
├── [ ] README.md — 1ч
├── [ ] Замена в docker-compose.yaml — 2ч
└── [ ] GET /pull с фильтрами — 1.5ч

Среднее
├── [ ] STATUS.md обновление — 0.5ч
├── [ ] Миграция клиентов с ZMQ — TBD
└── [ ] Telemetry file logging — 1ч

Низкое
└── [ ] Удаление мёртвого кода — 0.5ч

Оценка: ~13-15 часов до полной готовности
```

---

## Критерии готовности "можно заменить Python signaling"

1. ✅ RawEndpoint стабильно принимает соединения и обрабатывает heartbeat
2. ✅ HTTP endpoint отвечает на /health, /push, /stat
3. ✅ Подписки работают: client → subscribe → push → dispatch → subscriber получает событие
4. ✅ processQueryRequest реализован (парсит параметры, вызывает findEvents, отправляет результаты)
5. ✅ Reconnect реализован (exponential backoff, auto-reconnect, restore subscriptions)
6. ✅ **Совместимое REST API** — /v1/list/*, /v1/stat/clients реализованы
7. ❌ **LeaderConnector интеграция** — сервис регистрируется в leader
8. ❌ **Порт HTTP API синхронизирован** — 5557
9. ❌ **Docker healthcheck** — сервис проверяется Docker
10. ❌ **docker-compose.yaml обновлён** — signaling_cpp работает как основной сервис
11. ❌ **POST /v1/browse** — полная совместимость с BrowseRequest

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

## История обновлений

### 2026-07-06
- ✅ Реализовано совместимое REST API: GET /v1/list/* (tags, types, affected, owners)
- ✅ Реализован GET /v1/stat/clients — статистика клиентов и телеметрия
- ✅ Исправлен маппинг полей: `event_type` → `type`, `source` → `owner`
- ✅ Добавлена поддержка полей `type` и `owner` в `message_storage.cpp::get_unique_values()`
- ✅ Добавлены тесты в HttpTest с выводом ✅/❌
- ✅ Актуализирован статус компонентов и критериев готовности

### 2026-07-05
- Актуализирован статус компонентов
- Добавлен анализ совместимости с Python signaling API
- Добавлены задачи по cluster integration
- Добавлен LeaderConnector анализ
- Обновлено количество критических/высоких/средних задач
- Обновлено время оценки

---

*Создано: 28.06.2026*
*Обновлено: 05.07.2026 — актуализация для кластерной интеграции*