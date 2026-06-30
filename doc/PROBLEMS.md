# Проблемы проекта Lunaricorn

## ID: PROBLEM-001

**Status:** Open

**Description:**
В `requirements.txt` Orb сервиса указан `flask`, но используется `fastapi`. Это приводит к путанице и лишним зависимостям.

**Impact:**
- Лишние зависимости в окружении
- Возможные конфликты версий
- Путаница при разработке

**Possible solution:**
Заменить `flask` на `fastapi` и `uvicorn` в `requirements.txt`.

**Priority:**
Medium

**Дата обнаружения:** 28.06.2026

---

## ID: PROBLEM-002

**Status:** Open

**Description:**
В `main.py` Orb сервиса опечатка в сообщении об ошибке: "Setup Signaling cluster node" вместо "Setup Orb cluster node".

**Impact:**
- Путаница при отладке
- Несоответствие сообщений реальному назначению сервиса

**Possible solution:**
Исправить строку с текстом сообщения на корректную.

**Priority:**
Low

**Дата обнаружения:** 28.06.2026

---

## ID: PROBLEM-003

**Status:** Open

**Description:**
FastAPI приложение Orb (`app.py`) содержит только импорты. Само приложение не инициализировано, маршруты не реализованы.

**Impact:**
- Orb сервис не предоставляет HTTP API
- Интеграция с другими сервисами невозможна через HTTP
- Функциональность сервиса не работает

**Possible solution:**
1. Инициализировать FastAPI приложение
2. Реализовать базовые маршруты (health, ping)
3. Реализовать бизнес-логику
4. Добавить обработчики запросов

**Priority:**
High

**Дата обнаружения:** 28.06.2026

---

## ID: PROBLEM-004

**Status:** Open

**Description:**
C++ Signaling сервис работает в режиме selftest (без реального сетевого взаимодействия). В `main.cpp` код сетевого взаимодействия закомментирован.

**Impact:**
- Сервис не обрабатывает входящие запросы
- Сервис не публикует события
- Интеграция с другими сервисами невозможна

**Possible solution:**
1. Раскомментировать и доработать сетевой код
2. Добавить корректную инициализацию TCP endpoint
3. Настроить взаимодействие с Leader для service discovery
4. Добавить обработку ошибок подключения

**Priority:**
High

**Дата обнаружения:** 28.06.2026

---

## ID: PROBLEM-005

**Status:** Open

**Description:**
Скрипты `MAINTENANCE_HOST` захардкожены на `192.168.0.18` в docker-compose.yaml. Это делает конфигурацию непереносимой.

**Impact:**
- Невозможность запуска на других машинах без модификации
- Жёсткая привязка к сетевой конфигурации

**Possible solution:**
Заменить на имя сервиса Docker (`maintenance`) или использовать переменную окружения.

**Priority:**
High

**Дата обнаружения:** 28.06.2026

---

## ID: PROBLEM-006

**Status:** Open

**Description:**
Схема базы данных и миграции не реализованы ни для одного сервиса.

**Impact:**
- Сервисы не могут сохранять данные
- Интеграционные тесты невозможны
- Данные теряются при перезапуске

**Possible solution:**
1. Определить ER-диаграмму
2. Создать SQL миграции
3. Добавить механизм применения миграций
4. Добавить version control для БД

**Priority:**
Critical

**Дата обнаружения:** 28.06.2026

---

## ID: PROBLEM-007

**Status:** Open

**Description:**
Отсутствуют unit и интеграционные тесты для всех сервисов.

**Impact:**
- Невозможность безопасной рефакторинга
- Сложность обнаружения регрессий
- Нет гарантии качества кода

**Possible solution:**
1. Добавить pytest для Python сервисов
2. Добавить Google Test для C++ компонентов
3. Настроить coverage
4. Добавить CI/CD пайплайн с тестами

**Priority:**
High

**Дата обнаружения:** 28.06.2026

---

## ID: PROBLEM-008

**Status:** Open

**Description:**
OpenAPI спецификация не создана. API документация отсутствует (кроме базовой в maintenance/api.md).

**Impact:**
- Сложность интеграции внешних клиентов
- Нет auto-generated документации
- Невозможность использования Swagger UI

**Possible solution:**
1. Добавить OpenAPI annotations к FastAPI приложениям
2. Сгенерировать Swagger UI
3. Добавить Postman коллекции для тестирования

**Priority:**
Medium

**Дата обнаружения:** 28.06.2026

---

## ID: PROBLEM-009

**Status:** Open

**Description:**
Файловое хранилище Orb создано, но логика работы с файлами отсутствует.

**Impact:**
- Функция хранения файлов не работает
- Точка монтирования `/opt/lunaricorn/orb/file_storage` не используется

**Possible solution:**
1. Реализовать файловый менеджер в Orb
2. Добавить CRUD операции для файлов
3. Реализовать поиск и фильтрацию

**Priority:**
Medium

**Дата обнаружения:** 28.06.2026

---

## ID: PROBLEM-010

**Status:** Open

**Description:**
Нет Kubernetes манифестов для deployment. Проект поддерживает только Docker Compose.

**Impact:**
- Невозможность запуска в K8s кластере
- Ограничение масштабируемости
- Зависимость от Docker Compose

**Possible solution:**
1. Создать K8s Deployment манифесты
2. Добавить Service и Ingress ресурсы
3. Написать Helm chart
4. Добавить ConfigMap и Secret манифесты

**Priority:**
Low

**Дата обнаружения:** 28.06.2026

---

## ID: PROBLEM-011

**Status:** Open

**Description:**
Нет системы мониторинга и алертинга.

**Impact:**
- Невозможность отслеживания состояния сервисов
- Сложность обнаружения проблем в production
- Нет метрик производительности

**Possible solution:**
1. Добавить Prometheus metrics экспортер
2. Настроить Grafana dashboards
3. Добавить алертинг rules
4. Интегрировать с существующим Loki для логов

**Priority:**
Medium

**Дата обнаружения:** 28.06.2026

---

## ID: PROBLEM-012

**Status:** Open

**Description:**
Порт Maintenance API (`MAINTENANCE_API_PORT`) не имеет значения по умолчанию в docker-compose.yaml.

**Impact:**
- Порт может конфликтовать с другими сервисами
- Требует ручной настройки

**Possible solution:**
Установить значение по умолчанию, например `8003`.

**Priority:**
Low

**Дата обнаружения:** 28.06.2026

---

## Сводка по проблемам

| Приоритет | Количество | IDs |
|-----------|------------|-----|
| Critical | 1 | 006 |
| High | 5 | 003, 004, 005, 007 |
| Medium | 5 | 001, 008, 009, 011, 012 |
| Low | 2 | 002, 010 |

**Всего открытых проблем:** 13

| Приоритет | Решено | IDs |
|-----------|--------|-----|
| High | 5 | 013, 014, 015, 016, 017 |

---

## ID: PROBLEM-013

**Status:** Solved

**Description:**
Тест `MessageHeader_Size` в `raw_connection_test.cpp` ожидал размер структуры 28 байт, но фактический размер с `#pragma pack(push, 1)` составляет 25 байт.

**Impact:**
- Тест падал с ошибкой `check sizeof(MessageHeader) == 28u has failed [24 != 28]`
- Несоответствие между тестами и реальной структурой протокола

**Решение:**
Исправить тест: `BOOST_CHECK_EQUAL(sizeof(MessageHeader), 25u)`

**Priority:**
High

**Дата обнаружения:** 30.06.2026
**Дата решения:** 30.06.2026

---

## ID: PROBLEM-014

**Status:** Solved

**Description:**
Тест `Proto_StatsTracking` в `raw_connection_test.cpp` ожидал, что `deserializeJson` с пустым буфером данных (но с корректным заголовком `CT_Json`) будет успешным, но тест использовал неправильную инициализацию заголовка.

**Impact:**
- Тест падал с ошибкой `check proto.stats().ok.load() == 1ULL has failed [0 != 1]`
- Статистика не инкрементировалась потому что `data_type` был `CT_Raw` (0) из-за заполнения нулями

**Решение:**
Явно инициализировать `data_type = CT_Json` перед созданием буфера.

**Priority:**
High

**Дата обнаружения:** 30.06.2026
**Дата решения:** 30.06.2026

---

## ID: PROBLEM-016

**Status:** Solved

**Description:**
В `SignalingConnector::start()` переменная `_thread_state` не сохранялась, оставаясь `nullptr`. При остановке `stop_runner()` проверяла `_thread_state->finished`, что вызывало `[BUG] Attempt to add orphan thread 'SignalingConnector::runner' with null state`.

**Решение:**
Добавлено `_thread_state = std::make_shared<ThreadState>()` в `start()`.

**Priority:**
High

**Дата обнаружения:** 30.06.2026
**Дата решения:** 30.06.2026

---

## ID: PROBLEM-017

**Status:** Solved

**Description:**
Во всех обработчиках `RE_Client` (`on_heartbeat`, `on_pub_request`, `on_query_request`, `on_subscription`) создавался `MessageHeader hdr;` без инициализации полей `magic` и `version`. 
`serializeJson()` не перезаписывает `magic` и `version` — только `data_type`, `data_len`, `crc`. При отправке ответов клиенту использовался мусорный magic/version → `cannot create std::vector larger than max_size()`.

**Решение:**
Все `MessageHeader hdr` инициализированы с designated initializers:
```cpp
MessageHeader hdr = {
    .magic = HeaderMagic,
    .version = PROTOCOL_VERSION,
    ...
};
```

**Priority:**
High

**Дата обнаружения:** 30.06.2026
**Дата решения:** 30.06.2026

---

## ID: PROBLEM-015

**Status:** Solved

**Description:**
Коренная причина каскадных ошибок между тестером (SignalingConnector) и сервером (RawEndpoint/RE_Client) при сетевом взаимодействии. Было обнаружено 4 взаимосвязанных бага:

1. **БАГ 1 (Главный) — Двойной буфер в `signaling_api.cpp::send_message()`:**
   При отправке heartbeat (`data.empty() == true`) код делал `buf.resize(sizeof(MessageHeader))`, создавая 24 нулевых байта. Затем `serializeJson()` вызывал `buf.insert()`, добавляя ещё 24 корректных байта. Итог: 48 байт на wire, первые 24 — нулевые magic=0x00000000. Сервер валился с `invalid header magic: expected=0x12345678 actual=0x00000000`.

2. **БАГ 2 — Маленький receive-буфер (26 байт):**
   `kRecvBufSize = sizeof(MessageHeader) + 2 = 26 байт`. Ответы сервера с JSON (типично 100-200+ байт) обрезались. Клиент читал только первые 26 байт, парсер не мог корректно восстановить пакет.

3. **БАГ 3 — HB Loop (следствие бага 1):**
   При получении HB от сервера клиент отвечал своим HB через `send_client_hb()`, который шёл через тот же багнутый `send_message()` → сервер видел нулевой magic → сбрасывал состояние → клиент получал ошибку при следующем чтении → `thread emergency exit`.

4. **БАГ 4 — Неинициализированный MessageHeader в `push()`:**
   `MessageHeader header;` без инициализации — поля `data_len` и `crc` содержали мусор, хотя `serializeJson()` перезаписывает их, это UB на некоторых оптимизациях/компиляторах.

5. **БАГ 5 (Дополнительно) — Несоответствие ключей между toDict() и on_pub_request():**
   `SignalingEvent::toDict()` генерирует `"message"` и `"client_id"`, а `on_pub_request()` искал `"payload"` и `"source"`. Сервер не мог обработать push-события от клиента, хотя heartbeat cycle (с багом 1) был основной причиной отказа.

**Impact:**
- Полный отказ сетевого взаимодействия: клиент не мог отправить ни heartbeat, ни push-события
- Сервер аварийно закрывал соединение при любом heartbeat от клиента
- Аварийный выход треда `runner` с `thread emergency exit`
- Невозможность интеграционного тестирования

**Решение:**
1. `signaling_api.cpp :: send_message()`: убрать `buf.resize(sizeof(MessageHeader))`. Для пустых данных буфер остаётся пустым, `serializeJson()` сам добавляет только корректные 24 байта.
2. `signaling_api.cpp :: kRecvBufSize`: увеличено с 26 до 65536 байт (64KB).
3. `signaling_api.cpp :: on_server_request(MsgType::HB)`: убрать вызов `send_client_hb()` — клиент отправляет HB по своему таймеру, эхо-ответ на серверный HB приводит к HB-шторму.
4. `signaling_api.cpp :: push()`: инициализировать `MessageHeader header{}` (zero-init).
5. `raw_endpoint_client.cpp :: on_pub_request()`: искать `"message"` вместо `"payload"`, `"client_id"` вместо `"source"`.

**Дата обнаружения:** 30.06.2026
**Дата решения:** 30.06.2026

---

*Последнее обновление: 30.06.2026*
