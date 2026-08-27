приёмочные требования после выполнения которых можно считать сервис orb_cpp полностью готовым к использованию и переходить к следующим этапам разработки системы

## Phase 1: Базовый API

### API для C++ клиентов
- [x] можно создавать и новые объекты, менять существующие и получать объекты и метаинформацию по id
  - Реализовано через `IOrbController` интерфейс в `lunaricorn/cpp/orb_controller.h`
  - `OrbClient` в `lunaricorn/cpp/orb_client.h/cpp` — HTTP-клиент для взаимодействия с сервером
  - `OrbObject` в `lunaricorn/cpp/orb_object.h/cpp` — высокоуровневый объект с lazy-загрузкой
- [x] можно перемещаться между цепочками объектов
  - `follow_parent()`, `follow_prev()`, `follow_next()` в `OrbObject`

### API для Python клиентов
- [ ] можно создавать и новые объекты, менять существующие и получать объекты и метаинформацию по id
  - Статус: **не реализовано** — Python API библиотека отсутствует
  - Требуется: создать Python bindings для взаимодействия с orb_cpp сервером
- [ ] можно перемещаться между цепочками объектов
  - Статус: **не реализовано** — зависит от Python API

## Phase 2: Дополнительные возможности API

### Gzip сжатие
- [x] сервер поддерживает gzip сжатие ответов
  - Реализовано в `HttpEndpoint::client_accepts_gzip()`, `compress_gzip()`, `apply_gzip_if_needed()`

### Статистика API
- [x] сервер ведёт статистику обращений через API
  - Реализовано через `ApiStats` структуру в `http_endpoint.h`
  - Вывод统计 раз в 60 секунд через таймер

### Генерация ID
- [x] API генерации UUID (`GET /gen_id`)
  - Реализовано в `Engine::generate_id()` через Boost.UUID

### Поиск по тегам
- [x] API поиска объектов по тегам (`GET /search?tags=...`)
  - Реализовано в `MetaStorage::search_by_tags()` и `Engine::search_by_tags()`

### Автоматическое управление has_content
- [x] поле has_content автоматически управляется сервером
  - `Engine::update_has_blob()` синхронизирует has_content при store_blob

## Интеграция в кластер

### Leader интеграция
- [x] orb сервис является зарегистрированной нодой в leader
  - Реализовано в `main.cpp`: `LeaderConnector` с периодической регистрацией
  - Ожидает `CLUSTER_LEADER_URL` из переменных окружения
  - `wait_for_ready()` для ожидания готовности лидера
  - Периодическая регистрация экземпляра в leader
  - Поддержка `TEST_MODE=1` для отключения leader в тестовом режиме
- [ ] поддержка heartbeats (imalive сообщения)
  - Статус: **частично реализовано** — есть регистрация, но нужно проверить формат imalive сообщений

### Signaling интеграция
- [x] интеграция с signaling сервисом, orb создаёт события в signaling (fileop формата) при изменении объектов
  - Реализовано через `SignalConnector` в `signal_connector.h/cpp`
  - `Engine::store_meta()` отправляет `FileOp_new` при создании нового объекта и `FileOp_update` при обновлении
  - `Engine::store_blob()` отправляет `FileOp_update` при добавлении/обновлении blob данных
  - Конфигурация через переменные окружения: `SIGNALING_HOST`, `SIGNALING_REQ`, `SIGNALING_AGENT_ID`
  - Отказ от подключения к signaling не ломает работу сервиса (non-fatal)

## Документация

- [x] spec_phase1.md — спецификация первой фазы
- [x] spec_phase2.md — спецификация второй фазы
- [x] plan_phase1.md — план реализации первой фазы
- [x] plan_phase2.md — план реализации второй фазы
- [x] hld.md — High-Level Design документация
- [x] changelog.md — журнал изменений
- [x] goals.md — цели сервиса (актуализировано)

## Тестирование

- [x] CLI-тест (`OrbHttpTest`) в `cli/http_test.cpp`
  - Тесты: health, blob put/get, meta put/get, gen_id, search, gzip, has_blob_auto
  - Автоматический перезапуск циклов каждые 5 секунд

---

**Сервис:** services/orb_cpp
**Python и C++ API библиотеки для всех сервисов:** lunaricorn