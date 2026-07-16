# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Phase 2: Gzip-сжатие ответов API
- Phase 2: Статистика обращений API (ApiStats с таймером 60с)
- Phase 2: API генерации UUID (`GET /gen_id`)
- Phase 2: API поиска объектов по тегам (`GET /search?tags=...`)
- Phase 2: Автоматическое управление полем `has_content` в Engine
- Phase 2: Интерфейс `IOrbController` для клиентской части
- Phase 2: Класс `OrbClient` — HTTP-клиент для взаимодействия с orb_cpp сервером
- Phase 2: Класс `OrbObject` — высокоуровневый объект с lazy-загрузкой и навигацией по ссылкам
- Phase 2: CLI-тесты для gen_id, search, gzip, has_blob

### Changed
- `types.h`: добавлены `InternalMetaObject` (с полями parent, prev, next, tags, description, has_content) и `OrbData`
- `http_endpoint.h/cpp`: расширен роутинг, добавлены gzip, stats, gen_id, search
- `meta_storage.h/cpp`: добавлен метод `search_by_tags` с SQL `@>` оператором
- `engine.h/cpp`: добавлены `generate_id()`, `search_by_tags()`, `update_has_blob()`
- `CMakeLists.txt` (lunaricorn/cpp): добавлена цель `orb_client` с Boost.Beast и zlib
- `CMakeLists.txt` (cli): подключены `orb_client` и `lunaricorn_api` библиотеки

### Fixed
- Engine теперь проверяет существование meta перед сохранением blob
- has_content автоматически синхронизируется при store_blob и store_meta