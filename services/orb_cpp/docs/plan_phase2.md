# План реализации Phase 2

## Цели фазы
1. Реализация клиента `orb_client` в `lunaricorn/cpp`.
2. Расширение API сервера для поддержки новых возможностей клиента.
3. Тесты `orb_client` в CLI (старые тесты HTTP API сохранить).

---

## Таска 1: Gzip-сжатие в HttpEndpoint

**Файлы:** `app/http_endpoint.h`, `app/http_endpoint.cpp`

- [ ] Добавить в `HttpEndpoint` метод сжатия тела ответа через gzip (zlib или Boost.IOStreams).
- [ ] Проверять заголовок `Accept-Encoding: gzip` во входящих запросах.
- [ ] Если клиент поддерживает gzip — сжимать тело ответа и добавлять `Content-Encoding: gzip`.
- [ ] Клиент будет принудительно указывать `Accept-Encoding: gzip` во всех запросах.

---

## Таска 2: Статистика обращений API

**Файлы:** `app/http_endpoint.h`, `app/http_endpoint.cpp`, `app/engine.h`, `app/engine.cpp`

- [ ] Добавить в `Engine` или `HttpEndpoint` счётчики:
  - количество запросов по каждому эндпоинту (health, get_meta, put_meta, get_blob, put_blob, gen_id, search)
  - объём переданных данных (входящих/исходящих) в байтах
- [ ] Завести asio::steady_timer с интервалом 60 секунд.
- [ ] По срабатыванию таймера выводить сводную статистику в std::cout с меткой времени.
- [ ] Сбрасывать счётчики после каждого вывода (или накапливать, по выбору).

---

## Таска 3: API генерации ID

**Файлы:** `app/engine.h`, `app/engine.cpp`, `app/http_endpoint.h`, `app/http_endpoint.cpp`

- [ ] В `Engine` добавить метод `std::string generate_id()` — генерирует UUID (через Boost.UUID).
- [ ] В `HttpEndpoint` добавить обработчик `handle_gen_id` для `GET /gen_id`.
- [ ] Ответ: JSON `{"id": "сгенерированный-uuid"}`.
- [ ] Зарегистрировать маршрут в `create_response`.

---

## Таска 4: API поиска объектов

**Файлы:** `app/meta_storage.h`, `app/meta_storage.cpp`, `app/engine.h`, `app/engine.cpp`, `app/http_endpoint.h`, `app/http_endpoint.cpp`

- [ ] В `MetaStorage` добавить метод `std::vector<InternalMetaObject> search_by_tags(const std::vector<std::string>& tags)` — поиск по совпадению тегов в JSONB.
- [ ] В `Engine` добавить метод `search_by_tags(tags)`.
- [ ] В `HttpEndpoint` добавить обработчик `handle_search` для `GET /search?tags=tag1,tag2`.
- [ ] Ответ: JSON-массив объектов, удовлетворяющих поиску.
- [ ] Зарегистрировать маршрут в `create_response`.

---

## Таска 5: Автоматическое управление полем has_blob

**Файлы:** `app/engine.h`, `app/engine.cpp`, `app/blob_storage.h`, `app/blob_storage.cpp`

- [ ] При вызове `Engine::store_blob` — устанавливать `has_blob = true` в соответствующем `InternalMetaObject`.
- [ ] При вызове `Engine::delete_blob` — устанавливать `has_blob = false`.
- [ ] Если при `store_blob` мета-объект не найден — возвращать ошибку (blob без meta недопустим).

---

## Таска 6: Интерфейс IOrbController

**Файлы:** `lunaricorn/cpp/orb_controller.h` (новый)

- [ ] Создать заголовочный файл `orb_controller.h` в `lunaricorn/cpp/`.
- [ ] Определить абстрактный класс `IOrbController` с чисто виртуальными методами:
  - `health()` — проверка доступности
  - `get_meta(id)` — получение мета-объекта
  - `put_meta(id, data)` — сохранение мета-объекта
  - `get_blob(id)` — получение blob
  - `put_blob(id, data)` — сохранение blob
  - `search_by_tags(tags)` — поиск по тегам
  - `generate_id()` — генерация нового ID

---

## Таска 7: Класс orb_client

**Файлы:** `lunaricorn/cpp/orb_client.h`, `lunaricorn/cpp/orb_client.cpp` (новые)

- [ ] Создать класс `OrbClient`, реализующий `IOrbController`.
- [ ] Конструктор принимает URL сервера (строка вида `http://host:port`).
- [ ] Реализовать все методы `IOrbController` через HTTP-запросы (Boost.Beast / Boost.URL).
  - Во всех запросах указывать `Accept-Encoding: gzip`.
  - Обрабатывать gzip-ответы.
- [ ] Завести asio::steady_timer для периодического health check (интервал 5-10 секунд).
  - В случае недоступности — логировать.
  - При восстановлении — логировать.
- [ ] Метод `create()` — вызывает `/gen_id`, возвращает `OrbObject` с новым ID.
- [ ] Метод `search_by_id(id)` — вызывает `GET /meta/{id}`, возвращает `OrbObject`.
- [ ] Метод `search_by_tags(tags)` — вызывает `GET /search?tags=...`, возвращает `std::vector<OrbObject>`.

---

## Таска 8: Класс orb_object

**Файлы:** `lunaricorn/cpp/orb_object.h`, `lunaricorn/cpp/orb_object.cpp` (новые)

- [ ] Создать класс `OrbObject`, хранящий:
  - Ссылку на `IOrbController&`
  - `std::string id` (неизменяемый)
  - Кеш полей мета-объекта (загружается лениво с сервера)
- [ ] ID задаётся в конструкторе и не может быть изменён.
- [ ] Геттеры/сеттеры для полей: `parent`, `prev`, `next`, `description`, `tags` и т.д.
  - Геттеры: если поле не в кеше — запрашивают с сервера.
  - Сеттеры: записывают на сервер через PUT и обновляют кеш.
  - Поле `has_blob` — только геттер (управляется сервером автоматически).
- [ ] `check()` — выполняет GET /meta/{id}, возвращает true если объект существует.
- [ ] `store_blob(boost::json::object data)` — PUT /blob/{id}.
- [ ] `load_blob()` — GET /blob/{id}, возвращает `boost::json::object`.
- [ ] Функции следования по ссылкам:
  - `parent()` — получает объект родителя (новый `OrbObject`).
  - `prev()` — получает предыдущий объект.
  - `next()` — получает следующий объект.
  - Каждая функция возвращает `std::optional<OrbObject>` (nullopt если ссылка пуста или объект не найден).

---

## Таска 9: Адаптация CLI тестов

**Файлы:** `cli/http_test.h`, `cli/http_test.cpp`, `cli/main.cpp`

- [ ] В `OrbHttpTest`:
  - Сохранить существующие тесты сырого HTTP API (health, get/put blob/meta).
  - Добавить новые тесты через `OrbClient` и `OrbObject`:
    - health check через клиента
    - create → установка полей → чтение полей
    - create → store_blob → load_blob
    - создание цепочки parent/prev/next → следование по ссылкам
    - поиск по тегам
  - Каждый тест имеет счётчики успехов/ошибок.
- [ ] `main.cpp` — оставить прежнюю логику, выводить статистику и по старым, и по новым тестам.

---

## Таска 10: Обновление CMake и сборки

**Файлы:** `lunaricorn/cpp/CMakeLists.txt`, `services/orb_cpp/cli/CMakeLists.txt`, `services/orb_cpp/Dockerfile`, `services/orb_cpp/cli/build.sh`

- [ ] `lunaricorn/cpp/CMakeLists.txt`:
  - Добавить цели `orb_client`, `orb_object`, `orb_controller`.
  - Подключить зависимость от Boost.Beast, Boost.URL, Boost.UUID, zlib.
- [ ] `services/orb_cpp/cli/CMakeLists.txt`:
  - Подключить библиотеку `orb_client` к цели `orb_cli`.
- [ ] `Dockerfile`:
  - Убедиться, что в сборочном образе установлены zlib-dev и все необходимые Boost-компоненты.
- [ ] `cli/build.sh`:
  - Обновить для включения путей до `lunaricorn/cpp`.
- [ ] Обновить `services/orb_cpp/it.sh` если требуется.

---

## Таска 11: Документация

**Файлы:** `docs/changelog.md`, `docs/hld.md`, `docs/todo.md`

- [ ] `changelog.md`:
  - Добавить раздел `## [Unreleased]` с секциями `### Added`:
    - Gzip-сжатие в HttpEndpoint
    - Статистика обращений API
    - API генерации ID
    - API поиска по тегам
    - IOrbController, OrbClient, OrbObject
    - CLI-тесты orb_client
- [ ] `hld.md`:
  - Описать новые эндпоинты: `/gen_id`, `/search`, обновлённые ответы с gzip.
  - Описать архитектуру клиента: IOrbController, OrbClient, OrbObject.
  - Описать схему связей и навигации по ссылкам.
- [ ] `todo.md` (если актуален):
  - Отметить выполненные пункты Phase 2.
- [ ] `problems.md`:
  - Зафиксировать известные проблемы/ограничения, если обнаружены.

---

## Итоговый чеклист задач

- [ ] Таска 1: Gzip-сжатие в HttpEndpoint
- [ ] Таска 2: Статистика обращений API
- [ ] Таска 3: API генерации ID
- [ ] Таска 4: API поиска объектов
- [ ] Таска 5: Автоматическое управление полем has_blob
- [ ] Таска 6: Интерфейс IOrbController
- [ ] Таска 7: Класс orb_client
- [ ] Таска 8: Класс orb_object
- [ ] Таска 9: Адаптация CLI тестов
- [ ] Таска 10: Обновление CMake и сборки
- [ ] Таска 11: Документация