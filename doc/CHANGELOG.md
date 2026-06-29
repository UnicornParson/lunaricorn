# Журнал изменений проекта Lunaricorn

## 2026-06-28

### Добавлено

#### Документация
- Создана полная структура документации в `/doc/`
  - `STATUS.md` — текущее состояние проекта
  - `ARCHITECTURE.md` — архитектурная документация
  - `PROBLEMS.md` — реестр известных проблем
  - `ROADMAP.md` — дорожная карта развития
  - `orb.md` — документация Orb сервиса (существующая)

#### Signaling C++ Service
- **SignalingEngine** — ядро обработки событий
  - Создание событий (`createEvent`)
  - Получение уникальных значений (`getUniqueValues`)
  - Поиск событий с фильтрацией (`findEvents`)
  - Поиск событий по типу (`findEventsByType`)

- **MessageStorage** — слой хранения данных
  - Абстракция над PostgreSQL

- **EventDataExtendedTypeHandler** — обработчик расширенных типов
  - `event_data_extended_type_handler.h`
  - `event_data_extended_type_handler.cpp`

- **RawEndpoint** — сырой API эндпоинт
  - `raw_endpoint.h`
  - `raw_endpoint.cpp`
  - `raw_endpoint_client.h`

- **SignalingEngineTest** — механизм selftest
  - `signaling_engine_test.h`
  - `signaling_engine_test.cpp`

- **Система логирования MLog**
  - `maintenance.h` — LogCollectorClient и MLog
  - `mlog.cpp` — реализация логирования
  - Макросы: MLOG, MLOG_D, MLOG_W, MLOG_E, MBUG, MBUG_IF

- **Dockerfile для C++ сервиса**
  - `Dockerfile` — основной образ
  - `Dockerfile.base` — базовый образ
  - `Dockerfile.builder` — образ сборки
  - `Dockerfile.tester` — образ тестирования

- **Скрипты**
  - `it.sh` — скрипт интеграционных тестов
  - `make_app.sh` — сборка приложения
  - `make_base.sh` — сборка базового образа
  - `make_test.sh` — сборка тестового образа

#### Общие библиотеки
- **C++ Library (`lunaricorn/cpp/`)**
  - `maintenance.h` — LogCollectorClient и MLog
  - `mlog.cpp` — реализация MLog
  - DbConfig — конфигурация БД

### Добавлено

#### SignalingEngine — система подписок
- **subscribe()** — создание подписки клиента с фильтрами
  - `client_id` — уникальный идентификатор клиента
  - `types` — фильтр по типу события
  - `sources` — фильтр по источнику
  - `affected` — фильтр по affected
  - `tags` — фильтр по тегам
- **unsubscribe()** — удаление подписки по client_id
- **setOnSubEvent()** — установка колбэка для уведомлений
- **dispatchEvent()** — публичный метод для обработки события
- **on_event()** — приватный метод уведомления подписчиков
- **Subscriber** — структура подписчика с фильтрами и счётчиком
- **Система фильтрации** — проверка всех фильтров (type, source, affected, tags)

#### SignalingEngineTest — тесты подписок
- **testSubscribeUnsubscribe()** — тест создания/удаления подписок
- **testOnEventCallback()** — тест колбэка при совпадении фильтров
- **testOnEventFiltering()** — тест фильтрации по всем типам фильтров

### Изменено

#### SignalingEngine
- Убран `limit` из `Subscriber` и `subscribe()`
- Добавлен `#include <functional>`
- Добавлен `onSubEvent_` член

#### SignalingEngineTest
- Добавлены 3 новых теста для системы подписок

#### Документация
- `Design.md` — обновлена диаграмма потока подписок
- `Design.md` — добавлена структура Subscriber
- `Design.md` — добавлена диаграмма потоков подписок

#### Orb Service
- Добавлена детальная документация в `doc/orb.md`
- Структура:
  - `main.py` — точка входа
  - `app.py` — FastAPI каркас
  - `node_config.py` — регистрация узла
  - `logger_config.py` — логирование
  - `internal/orb_types.py` — конфигурация

#### Docker инфраструктура
- `docker-compose.yaml` — конфигурация всех сервисов
- Скрипты запуска отдельных сервисов
  - `leader_up.sh`
  - `signaling_up.sh`
  - `orb_up.sh`
  - `portal_up.sh`
  - `up.sh`
  - `down.sh`

### Известные проблемы
- Добавлено 12 проблем в `PROBLEMS.md`
- Критическая: PROBLEM-006 — нет схемы БД

---

## 2026-01-01 (предыдущие изменения)

### Добавлено
- Базовая структура проекта
- Leader сервис (Python + FastAPI)
- Signaling сервис (Python + ZeroMQ)
- Orb сервис (Python + FastAPI каркас)
- Portal сервис (Python + FastAPI)
- Maintenance сервис (C++ + Poco)
- PostgreSQL база данных
- Общая C++ библиотека
- Общая Python библиотека
- Docker контейнеризация
- Базовая сеть кластера

### Изменено
- Инициализация репозитория
- Начальная настройка проекта

---

## Статус версий

| Версия | Дата | Статус |
|--------|------|--------|
| 0.3 | 29.06.2026 | Система подписок |
| 0.2 | 28.06.2026 | Текущая разработка |
| 0.1 | 2026-01-01 | Архивная |

---

*Последнее обновление: 28.06.2026*