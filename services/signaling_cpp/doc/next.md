# Next: Миграция на signaling_cpp как основной сервис signaling

---

## 1. Реализовать POST /v1/browse (совместимо с Python API)

**Статус:** ❌ Не реализовано

**Проблема:** Python signaling поддерживает `POST /v1/browse` с `BrowseRequest` body:
```json
{
  "event_types": ["type1", "type2"],
  "sources": ["src1"],
  "affected": ["item1"],
  "tags": ["tag1"],
  "timestamp": 1234567890,
  "limit": 100
}
```

C++ HTTP endpoint поддерживает только `GET /v1/pull?offset=N` — без фильтров.

**Задачи:**
- [ ] Добавить `BrowseRequest` парсер в `http_endpoint.cpp::handle_browse()`
- [ ] Парсить JSON body: `event_types`, `sources`, `affected`, `tags`, `timestamp`, `limit`
- [ ] Вызвать `engine->findEvents()` с фильтрами
- [ ] Вернуть массив событий в формате:
```json
{
  "events": [
    {
      "eid": 1,
      "type": "event_type",
      "payload": {},
      "affected": [],
      "tags": [],
      "source": "src",
      "timestamp": 1234567890
    }
  ],
  "count": 1
}
```
- [ ] Добавить тест в `HttpTest::test_browse()`
- [ ] Добавить запись в changelog.md

**Файлы:** `app/http_endpoint.cpp`, `app/http_endpoint.h`, `cli/http_test.cpp`, `doc/changelog.md`

---

## 2. Добавить переменные окружения для портов

**Статив:** ❌ Порт HTTP жёстко задан как `8081`

**Задачи:**
- [ ] Добавить `SIGNALING_RAW_PORT` (по умолчанию `8080`)
- [ ] Добавить `SIGNALING_API_PORT` (по умолчанию `8081`)
- [ ] Прочитать порты из env в `main.cpp`
- [ ] Обновить `docker-compose.yaml` для signaling_cpp с нужными портами
- [ ] Добавить запись в changelog.md

**Файлы:** `app/main.cpp`, `doc/changelog.md`

---

## 3. Добавить Docker healthcheck

**Статус:** ❌ Отсутствует

**Задачи:**
- [ ] Добавить `HEALTHCHECK CMD curl -f http://localhost:8081/health || exit 1` в `Dockerfile`
- [ ] Добавить healthcheck в `docker-compose.yaml` для сервиса `signaling_cpp`
- [ ] Убедиться, что `curl` доступен в финальном образе
- [ ] Добавить запись в changelog.md

**Файлы:** `Dockerfile`, `../docker-compose.yaml`, `doc/changelog.md`

---

## 4. Создать README.md

**Статус:** ❌ Отсутствует

**Содержание:**
- [ ] Prerequisites (C++20, CMake, Boost, Poco, soci, PostgreSQL)
- [ ] Build (cmake, make)
- [ ] Configuration (переменные окружения: DB_*, CLUSTER_LEADER_URL, SIGNALING_RAW_PORT, SIGNALING_API_PORT)
- [ ] Run (локально, docker)
- [ ] Testing (CLI клиент, HTTP endpoint)
- [ ] Cluster integration (CLUSTER_LEADER_URL, регистрация в лидере)
- [ ] API reference (кратко)

**Файл:** `README.md`

---

## 5. Интегрировать signaling_cpp в docker-compose.yaml

**Статус:** ❌ Сервис `signaling_cpp` отсутствует в compose

**Задачи:**
- [ ] Добавить сервис `signaling_cpp` в `docker-compose.yaml`:
  - `build: ./signaling_cpp`
  - `container_name: lunaricorn-signaling-cpp`
  - `ports: "${SIGNALING_RAW_PORT:-8080}:8080", "${SIGNALING_API_PORT:-8081}:8081"`
  - `environment: CLUSTER_LEADER_URL=http://leader:8000/, DB_*, MAINTENANCE_*`
  - `depends_on: pg (healthy), leader (healthy), maintenance (healthy)`
  - `networks: lunaricorn-network`
  - `healthcheck: curl -f http://localhost:8081/health`
- [ ] Добавить переменные `SIGNALING_RAW_PORT`, `SIGNALING_API_PORT` в `.env` или в compose
- [ ] Добавить запись в changelog.md

**Файл:** `../docker-compose.yaml`, `doc/changelog.md`

---

## 6. Обновить orb для использования HTTP вместо ZMQ

**Статус:** ⚠️ Требуется анализ

**Текущее состояние orb:**
- Использует `SIGNALING_REQ=5555` (ZMQ REP socket)
- Использует `SIGNALING_PUB=5556` (ZMQ PUB socket)
- Использует `SIGNALING_API=5557` (HTTP API)

**Задачи:**
- [ ] Найти в orb коде все места где используются ZMQ connections к signaling
- [ ] Заменить ZMQ push на `POST /v1/push` HTTP
- [ ] Заменить ZMQ subscribe на polling `GET /v1/pull` или SSE/WebSocket
- [ ] Или: оставить ZMQ для Python signaling на период миграции
- [ ] Обновить `SIGNALING_API` переменную для использования нового порта
- [ ] Добавить запись в changelog.md

**Файлы:** `services/orb/` (искать ZMQ код)

---

## 7. Миграция Python клиентов на HTTP

**Статус:** ⚠️ Требуется действие

**Задачи:**
- [ ] В Python signaling клиенте убрать весь ZMQ код (REP socket)
- [ ] Заменить ZMQ push на HTTP `POST /v1/push`
- [ ] Заменить ZMQ subscribe на HTTP polling / SSE
- [ ] Удалить `zmq_server.py` после миграции
- [ ] Обновить `main.py` — удалить `ZeroMQSignalingServer`
- [ ] Добавить запись в changelog.md

**Файлы:** `services/signaling/` (api_server.py, main.py, zmq_server.py)

---

## 8. Обновить документацию

**Статус:** ⚠️ Частично актуально

**Задачи:**
- [ ] Обновить `problems.md` — удалить решённые проблемы
- [ ] Обновить `TODO.md` — статус компонентов
- [ ] Обновить корневой `doc/STATUS.md`
- [ ] Добавить запись в changelog.md

**Файлы:** `doc/problems.md`, `doc/TODO.md`, `../../doc/STATUS.md`, `doc/changelog.md`

---

## 9. Добавить интеграционные тесты

**Статус:** ⚠️ IT-скрипт существует, но не завершён

**Задачи:**
- [ ] Доработать `it.sh` для тестирования HTTP API
- [ ] Добавить тест `POST /v1/browse`
- [ ] Добавить тест подписок через raw-протокол
- [ ] Добавить тест graceful shutdown
- [ ] Добавить запись в changelog.md

**Файл:** `../test/it.sh`, `doc/changelog.md`

---

## 10. Финальная проверка и переключение

**Задачи:**
- [ ] Запустить signaling_cpp в параллель с Python signaling (blue-green)
- [ ] Протестировать所有 эндпоинты через HTTP
- [ ] Убедиться, что orb работает с новым signaling
- [ ] Отключить Python signaling
- [ ] Удалить `zmq_server.py` из репозитория
- [ ] Добавить запись в changelog.md

---

## Оценка

| Задача | Оценка | Блокирует? |
|--------|--------|-----------|
| 1. POST /v1/browse | 2ч | ✅ Да |
| 2. Переменные портов | 0.5ч | ✅ Да |
| 3. Docker healthcheck | 1ч | ❌ Нет |
| 4. README.md | 1ч | ❌ Нет |
| 5. docker-compose.yaml | 2ч | ✅ Да |
| 6. Обновить orb | TBD | ✅ Да |
| 7. Миграция Python клиентов | TBD | ✅ Да |
| 8. Обновить документацию | 1ч | ❌ Нет |
| 9. Интеграционные тесты | 2ч | ❌ Нет |
| 10. Финальная проверка | 1ч | ✅ Да |

**Итого:** ~10 часов + TBD на миграцию orb и клиентов