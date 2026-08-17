# План реализации Phase 2 и Оценка Готовности Интеграции

## Оценка готовности интеграции orb_cpp в общий кластер

### 1. Серверная часть (app/)

| Критерий | Статус | Комментарии |
|----------|--------|-------------|
| Gzip-сжатие | ✅ Реализовано | `client_accepts_gzip()`, `compress_gzip()`, `apply_gzip_if_needed()` в `http_endpoint.cpp` |
| Статистика API | ✅ Реализована | `ApiStats` с атомарными счётчиками, таймер 60с, вывод в консоль через `MLOG_D` |
| API генерации UUID | ✅ Реализовано | `GET /gen_id` через `Engine::generate_id()` (Boost.UUID) |
| API поиска по тегам | ✅ Реализовано | `GET /search?tags=...` через `MetaStorage::search_by_tags()` с SQL `@>` оператором |
| Авто-управление has_content | ✅ Реализовано | `update_has_blob()` в Engine при `store_blob`/`store_meta` |
| Роутинг HTTP | ✅ Реализован | Все эндпоинты: `/health`, `/gen_id`, `/search`, `/blob/{id}`, `/meta/{id}` |
| Обработка ошибок | ✅ Реализована | try/catch, status codes (404, 500, 400) |
| Конфигурация БД | ✅ Готово | Через env vars: `DB_HOST`, `DB_PORT`, `DB_USER`, `DB_PASSWORD`, `DB_NAME` |

**Готовность сервера: 100%**

---

### 2. Клиентская часть (lunaricorn/cpp/)

| Критерий | Статус | Комментарии |
|----------|--------|-------------|
| IOrbController интерфейс | ✅ Реализован | `orb_controller.h`: `OrbMetaData`, все методы IOrbController |
| OrbClient | ✅ Реализован | `orb_client.h/cpp`: HTTP-клиент с gzip, health check таймер (10с), status callback |
| OrbObject | ✅ Реализован | `orb_object.h/cpp`: lazy-loading, setters/getters, blob ops, navigation (parent/prev/next) |
| Сжатие/расжатие | ✅ Реализовано | `compress_gzip()` в сервере, `decompress_gzip()` в клиенте |
| Парсинг URL | ✅ Реализован | `parse_url()` поддерживает `http://host:port/path` |
| Поддержка префикса пути | ✅ Реализована | `target_prefix_` для API версионирования |

**Готовность клиента: 100%**

---

### 3. Сборка и зависимости

| Критерий | Статус | Комментарии |
|----------|--------|-------------|
| CMake (lunaricorn/cpp) | ✅ Готов | `orb_client` library с Boost.Beast, zlib, lunaricorn_api |
| CMake (cli) | ✅ Готов | orb_cli подключает orb_client и lunaricorn_api |
| Dockerfile (builder) | ✅ Готов | `lunaricorn_orb_cpp_builder` с Boost, Poco, SOCI, zlib |
| Dockerfile (image) | ✅ Готов | orb + orb_cli, пути библиотек, ldconfig |
| Dockerfile.builder | ✅ Готов | Базовый образ для сборки |
| Зависимости Boost | ✅ Указаны | json, beast, system, filesystem, UUID (нужен для gen_id) |
| Зависимость zlib | ✅ Указана | `find_package(ZLIB REQUIRED)` |

**Готовность сборки: 95%** — Boost.UUID не указан в `find_package(Boost COMPONENTS ...)` в CMakeLists.txt (строка 13). Нужно добавить `uuid` в список компонентов.

---

### 4. Тестирование

| Критерий | Статус | Комментарии |
|----------|--------|-------------|
| CLI тесты (сырой HTTP) | ✅ Реализованы | `http_test.h/cpp`: health, blob, meta, gen_id, search, gzip, has_blob |
| CLI тесты (OrbClient) | ⚠️ Частично | `http_test.h` объявляет методы, но `http_test.cpp` использует сырой Poco HTTP, а не OrbClient |
| Интеграционные тесты | ⚠️ Нет | `it.sh` требует проверки на актуальность |
| Unit-тесты | ❌ Нет | В `problems.md` отмечено: "инфраструктура make_test не настроена" |

**Готовность тестирования: 60%** — сырые HTTP тесты есть, но тесты через OrbClient/OrbObject не реализованы.

---

### 5. Документация

| Критерий | Статус | Комментарии |
|----------|--------|-------------|
| changelog.md | ✅ Актуален | Все изменения Phase 2 задокументированы |
| hld.md | ⚠️ Частично | Нет новых эндпоинтов `/gen_id`, `/search`, gzip, архитектуры клиента |
| problems.md | ⚠️ Устарел | Нет проблем Phase 2 |
| todo.md | ✅ Есть | Задачи 1-2 (list_meta/list_data, connectivity check) |
| spec_phase1.md | ✅ Есть | |
| spec_phase2.md | ✅ Есть | |

**Готовность документации: 70%** — hld.md и problems.md требуют обновления.

---

### 6. Интеграция с кластером (docker-compose.yaml)

| Критерий | Статер | Комментарии |
|----------|--------|-------------|
| Сервис в docker-compose | ❌ Нет | orb_cpp НЕ добавлен в `services/docker-compose.yaml` |
| Порт HTTP API | ? | Сервер слушает 8081 (по умолчанию), нужно сопоставить порт |
| Healthcheck | ⚠️ Частично | `/health` endpoint есть, healthcheck будет работать |
| Зависимости (pg) | ✅ Готово | Подключится к `lunaricorn-pg` как signaling_cpp |
| Сеть | ✅ Готово | Подключится к `lunaricorn-network` |
| Env vars (DB) | ✅ Готово | Поддерживает `db_host`, `db_port`, `db_user`, `db_password`, `db_name`, `db_schema` |
| Leader integration | ⚠️ Нет | Нет `CLUSTER_LEADER_URL` и `MAINTENANCE_*` переменных |
| Volumes | ⚠️ Нет | Нет конфигурации томов для данных |
| Имя контейнера | ? | Нужно задать `container_name` |

**Готовность интеграции в кластер: 40%** —最关键ная проблема: сервис НЕ добавлен в docker-compose.yaml.

---

### 7. Сравнение с signaling_cpp

| Аспект | signaling_cpp | orb_cpp | Разница |
|--------|--------------|---------|---------|
| Протокол | Binary + HTTP | HTTP REST | orb_cpp проще для интеграции |
| Порт API | 8081 (HTTP) | 8081 (HTTP) | Конфликт портов при одновременном запуске |
| Healthcheck | `/health` на 8081 | `/health` на 8081 | Аналогично |
| DB конфиг | `db_*` env vars | `db_*` env vars | Совместимо |
| Leader | `CLUSTER_LEADER_URL` | ❌ Нет | orb_cpp не знает о кластере |
| Maintenance | `MAINTION_*` env vars | ❌ Нет | orb_cpp не интегрирован с maintenance |
| Dockerfile | builder + tester + cli | builder + cli_builder | signaling_cpp имеет tester |
| Тесты | signaling_cli | http_test | orb_cpp тесты только HTTP |

---

## ИТОГОВАЯ ОЦЕНКА

| Категория | Готовность |
|-----------|------------|
| Серверная часть (API) | 100% |
| Клиентская часть (OrbClient) | 100% |
| Сборка (CMake/Docker) | 95% |
| Тестирование | 60% |
| Документация | 70% |
| Интеграция в кластер | 40% |
| **ОБЩАЯ** | **~75%** |

---

# План реализации Phase 2 (продолжение)

## Цели фазы
1. Реализация клиента `orb_client` в `lunaricorn/cpp`.
2. Расширение API сервера для поддержки новых возможностей клиента.
3. Тесты `orb_client` в CLI (старые тесты HTTP API сохранить).

---

## Таска 1: Gzip-сжатие в HttpEndpoint

**Файлы:** `app/http_endpoint.h`, `app/http_endpoint.cpp`

- [x] Добавить в `HttpEndpoint` метод сжатия тела ответа через gzip (zlib).
- [x] Проверять заголовок `Accept-Encoding: gzip` во входящих запросах.
- [x] Если клиент поддерживает gzip — сжимать тело ответа и добавлять `Content-Encoding: gzip`.
- [x] Клиент будет принудительно указывать `Accept-Encoding: gzip` во всех запросах.

---

## Таска 2: Статистика обращений API

**Файлы:** `app/http_endpoint.h`, `app/http_endpoint.cpp`, `app/engine.h`, `app/engine.cpp`

- [x] Добавить в `Engine` или `HttpEndpoint` счётчики:
  - [x] количество запросов по каждому эндпоинту
  - [x] объём переданных данных (входящих/исходящих) в байтах
- [x] Завести asio::steady_timer с интервалом 60 секунд.
- [x] По срабатыванию таймера выводить сводную статистику в std::cout.
- [x] Сбрасывать счётчики после каждого вывода.

---

## Таска 3: API генерации ID

**Файлы:** `app/engine.h`, `app/engine.cpp`, `app/http_endpoint.h`, `app/http_endpoint.cpp`

- [x] В `Engine` добавить метод `std::string generate_id()` — генерирует UUID (через Boost.UUID).
- [x] В `HttpEndpoint` добавить обработчик `handle_gen_id` для `GET /gen_id`.
- [x] Ответ: JSON `{"id": "сгенерированный-uuid"}`.
- [x] Зарегистрировать маршрут в `create_response`.

---

## Таска 4: API поиска объектов

**Файлы:** `app/meta_storage.h`, `app/meta_storage.cpp`, `app/engine.h`, `app/engine.cpp`, `app/http_endpoint.h`, `app/http_endpoint.cpp`

- [x] В `MetaStorage` добавить метод `search_by_tags()` — поиск по совпадению тегов в JSONB.
- [x] В `Engine` добавить метод `search_by_tags(tags)`.
- [x] В `HttpEndpoint` добавить обработчик `handle_search` для `GET /search?tags=tag1,tag2`.
- [x] Ответ: JSON-массив объектов, удовлетворяющих поиску.
- [x] Зарегистрировать маршрут.

---

## Таска 5: Автоматическое управление полем has_blob

**Файлы:** `app/engine.h`, `app/engine.cpp`, `app/blob_storage.h`, `app/blob_storage.cpp`

- [x] При вызове `Engine::store_blob` — устанавливать `has_content = true`.
- [x] При вызове `Engine::delete_blob` — устанавливать `has_content = false`.
- [x] Если при `store_blob` мета-объект не найден — возвращать ошибку.

---

## Таска 6: Интерфейс IOrbController

**Файлы:** `lunaricorn/cpp/orb_controller.h` (новый)

- [x] Создать заголовочный файл `orb_controller.h`.
- [x] Определить `OrbMetaData` (структура с полями meta-объекта).
- [x] Определить абстрактный класс `IOrbController` с методами:
  - [x] `health()`
  - [x] `get_meta(id)`
  - [x] `put_meta(id, data)`
  - [x] `get_blob(id)`
  - [x] `put_blob(id, data)`
  - [x] `search_by_tags(tags)`
  - [x] `generate_id()`

---

## Таска 7: Класс orb_client

**Файлы:** `lunaricorn/cpp/orb_client.h`, `lunaricorn/cpp/orb_client.cpp` (новые)

- [x] Создать класс `OrbClient`, реализующий `IOrbController`.
- [x] Конструктор принимает URL сервера (строка вида `http://host:port`).
- [x] Реализовать все методы `IOrbController` через HTTP-запросы (Boost.Beast).
  - [x] Во всех запросах указывать `Accept-Encoding: gzip`.
  - [x] Обрабатывать gzip-ответы.
- [x] Завести asio::steady_timer для periodic health check (интервал 10 секунд).
  - [x] Логировать статус при изменении.
- [x] `generate_id()` — вызывает `/gen_id`.
- [x] `get_meta(id)` — вызывает `GET /meta/{id}`.
- [x] `search_by_tags(tags)` — вызывает `GET /search?tags=...`.

---

## Таска 8: Класс orb_object

**Файлы:** `lunaricorn/cpp/orb_object.h`, `lunaricorn/cpp/orb_object.cpp` (новые)

- [x] Создать класс `OrbObject`, хранящий:
  - [x] Ссылку на `IOrbController&`
  - [x] `std::string id` (неизменяемый)
  - [x] Кеш полей мета-объекта (lazy-load)
- [x] ID задаётся в конструкторе и не может быть изменён.
- [x] Геттеры/сеттеры для полей: `parent`, `prev`, `next`, `description`, `tags`.
  - [x] Геттеры: если поле не в кеше — запрашивают с сервера.
  - [x] Сеттеры: записывают на сервер через PUT и обновляют кеш.
  - [x] Поле `has_content` — только геттер.
- [x] `check()` — выполняет GET /meta/{id}, возвращает true если объект существует.
- [x] `store_blob(data)` — PUT /blob/{id}.
- [x] `load_blob()` — GET /blob/{id}.
- [x] Функции навигации:
  - [x] `follow_parent()` — возвращает `std::optional<OrbObject>`
  - [x] `follow_prev()` — возвращает `std::optional<OrbObject>`
  - [x] `follow_next()` — возвращает `std::optional<OrbObject>`

---

## Таска 9: Адаптация CLI тестов

**Файлы:** `cli/http_test.h`, `cli/http_test.cpp`, `cli/main.cpp`

- [x] В `OrbHttpTest`:
  - [x] Сохранить существующие тесты сырого HTTP API.
  - [x] Добавить тесты: health, gen_id, search, gzip, has_blob_auto.
- [x] `main.cpp` — логика тестов, вывод статистики.
- [ ] **ДОПОЛНИТЬ**: Добавить тесты через `OrbClient` и `OrbObject`:
  - [ ] create → установка полей → чтение полей
  - [ ] create → store_blob → load_blob
  - [ ] создание цепочки parent/prev/next → следование по ссылкам
  - [ ] поиск по тегам через клиента

---

## Таска 10: Обновление CMake и сборки

**Файлы:** `lunaricorn/cpp/CMakeLists.txt`, `services/orb_cpp/cli/CMakeLists.txt`, `services/orb_cpp/Dockerfile`, `services/orb_cpp/cli/build.sh`

- [x] `lunaricorn/cpp/CMakeLists.txt`:
  - [x] Добавить цель `orb_client` (orb_client.cpp + orb_object.cpp).
  - [x] Подключить зависимость от Boost.Beast, zlib.
- [x] `services/orb_cpp/cli/CMakeLists.txt`:
  - [x] Подключить библиотеку `orb_client` к цели `orb_cli`.
- [x] `Dockerfile`:
  - [x] Сборка orb и orb_cli.
  - [x] Копирование библиотек Boost и Poco.
- [x] `cli/build.sh`:
  - [x] Обновлён для компиляции.
- [ ] **ИСПРАВИТЬ**: Добавить `uuid` в `find_package(Boost COMPONENTS ...)` в `lunaricorn/cpp/CMakeLists.txt`.

---

## Таска 11: Документация

**Файлы:** `docs/changelog.md`, `docs/hld.md`, `docs/problems.md`, `docs/todo.md`

- [x] `changelog.md`:
  - [x] Добавить раздел `## [Unreleased]` с секциями `### Added`:
    - [x] Gzip-сжатие в HttpEndpoint
    - [x] Статистика обращений API
    - [x] API генерации ID
    - [x] API поиска по тегам
    - [x] IOrbController, OrbClient, OrbObject
    - [x] CLI-тесты orb_client
- [ ] `hld.md`:
  - [ ] Описать новые эндпоинты: `/gen_id`, `/search`.
  - [ ] Описать gzip-сжатие и статистику.
  - [ ] Описать архитектуру клиента: IOrbController, OrbClient, OrbObject.
  - [ ] Описать схему навигации по ссылкам.
- [ ] `problems.md`:
  - [ ] Добавить раздел Phase 2: известные проблемы и ограничения.
- [x] `todo.md` — актуализировать при необходимости.

---

## Таска 12: Интеграция в кластер (docker-compose.yaml)

**Файлы:** `services/docker-compose.yaml`

- [ ] Добавить сервис `orb_cpp`:
  - [ ] `build.context: ./orb_cpp`
  - [ ] `container_name: lunaricorn-orb-cpp`
  - [ ] `ports: "8082:8081"` (или настраиваемый порт)
  - [ ] `environment:`:
    - [ ] `db_host=lunaricorn-pg`
    - [ ] `db_port=5432`
    - [ ] `db_user=lunaricorn`
    - [ ] `db_password=${LUNARICORN_PASSWORD}`
    - [ ] `db_name=lunaricorn`
    - [ ] `db_schema=lunaricorn`
    - [ ] `CLUSTER_LEADER_URL=http://leader:8000/`
    - [ ] `MAINTENANCE_HOST=192.168.0.18`
    - [ ] `MAINTENANCE_PORT=${MAINTENANCE_API_PORT}`
  - [ ] `volumes:` для данных
  - [ ] `networks: lunaricorn-network`
  - [ ] `depends_on: pg (healthy), leader (healthy)`
  - [ ] `healthcheck: curl -f http://localhost:8081/health`
  - [ ] `restart: unless-stopped`
- [ ] Добавить `orb_cpp_builder` в `build_base.sh` если нужно.

---

## Таска 13: Обновление скриптов запуска

**Файлы:** `services/orb_up.sh`, `services/build.sh`, `services/up.sh`

- [ ] Обновить `orb_up.sh` для запуска orb_cpp (или заменить orb на orb_cpp).
- [ ] Обновить `build.sh` для сборки orb_cpp.
- [ ] Обновить `up.sh` для запуска orb_cpp.

---

## Итоговый чеклист задач

### Реализовано (Phase 2):
- [x] Таска 1: Gzip-сжатие в HttpEndpoint
- [x] Таска 2: Статистика обращений API
- [x] Таска 3: API генерации ID
- [x] Таска 4: API поиска объектов
- [x] Таска 5: Автоматическое управление полем has_blob
- [x] Таска 6: Интерфейс IOrbController
- [x] Таска 7: Класс orb_client
- [x] Таска 8: Класс orb_object
- [x] Таска 9 (частично): CLI тесты HTTP API
- [x] Таска 11 (частично): changelog.md

### Требуют реализации:
- [ ] Таска 9 (дополнить): Тесты через OrbClient/OrbObject
- [ ] Таска 10 (исправить): Добавить Boost.UUID в CMake
- [ ] Таска 11 (дополнить): Обновить hld.md, problems.md
- [ ] Таска 12: Добавить orb_cpp в docker-compose.yaml
- [ ] Таска 13: Обновить скрипты запуска