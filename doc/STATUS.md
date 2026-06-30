# Текущий статус проекта Lunaricorn

## Фаза
Активная разработка MVP

## Общая архитектура

Проект **Lunaricorn** — это кластерная микросервисная платформа с самоорганизующейся инфраструктурой. Система включает:

- **Leader** — сервис управления кластером (выбор лидера, регистрация узлов)
- **Signaling** — сервис сигнализации (ZeroMQ REQ-REP/PUB-SUB + HTTP API)
- **Signaling C++** — высокопроизводительная реализация сигнализационного сервиса на C++
- **Orb** — орбитальный сервис (REST/GRPC API, файловое хранилище)
- **Portal** — веб-портал управления
- **Maintenance** — сервис обслуживания и сбора логов
- **PostgreSQL** — общая база данных
- **Lunaricorn C++ Library** — общая библиотека (логирование, работа с БД, сеть)

## Реализовано

### Python-сервисы

#### Leader Service
- HTTP API на FastAPI (порт 8001)
- Регистрация узлов в кластере
- Health check эндпоинт
- Docker контейнеризация

#### Signaling Service (Python)
- ZeroMQ REQ-REP на порту 5555
- ZeroMQ PUB-SUB на порту 5556
- HTTP API на порту 5557
- Интеграция с Leader API
- Интеграция с PostgreSQL
- Тестовые клиенты

#### Signaling Service (C++)
- SignalingEngine — ядро обработки событий
- MessageStorage — слой хранения данных
- EventDataExtendedTypeHandler — обработчик расширенных типов
- RawEndpoint — серверный сырой API эндпоинт
- RE_Client — клиентский сырой API эндпоинт
- Система логирования MLog
- Selftest механизм
- Поддержка конфигурации из переменных окружения
- **Система подписок**
  - `subscribe()` — создание подписки с фильтрами (type, source, affected, tags)
  - `unsubscribe()` — удаление подписки
  - `setOnSubEvent()` — установка колбэка уведомлений
  - `dispatchEvent()` — обработка события и уведомление подписчиков
  - Subscriber — структура с фильтрами и счётчиком

#### Orb Service
- FastAPI приложение (каркас)
- NodeController — регистрация узла в кластере
- Система логирования с ротацией
- Docker контейнеризация
- Конфигурация из переменных окружения

#### Portal Service
- FastAPI приложение
- Интеграция с Leader API
- Статические файлы
- Docker контейнеризация

#### Maintenance Service
- C++ backend (Poco framework)
- LogCollectorClient — сбор логов
- Health check API
- Docker контейнеризация
- workers для обработки (WORKERS=4)

### Общие библиотеки

#### C++ Library (`lunaricorn/cpp/`)
- `MLog` — система логирования с макросами (MLOG, MLOG_D, MLOG_W, MLOG_E)
- `LogCollectorClient` — клиент для сбора логов Maintenance сервиса
- `DbConfig` — конфигурация подключения к PostgreSQL
- Макросы: MLOG, MLOG_D, MLOG_W, MLOG_E, MBUG, MBUG_IF

#### Python Library (`lunaricorn/`)
- `api/` — API клиенты (leader, signaling)
- `cpp/` — C++ интеграция
- `data/` — работа с данными
- `net/` — сетевые утилиты
- `types/` — общие типы
- `utils/` — утилиты


## Не реализовано

1. **API Endpoints Orb** — HTTP маршруты в Orb сервисе не реализованы
2. **База данных** — схема БД и миграции не реализованы
3. **Бизнес-логика Orb** — функциональность не определена
4. **Тесты** — интеграционные тесты отсутствуют
5. **Документация API** — OpenAPI спецификация не создана
6. **Файловое хранилище Orb** — логика работы отсутствует
7. **Kubernetes deployment** — нет манифестов
8. **Monitoring** — система мониторинга отсутствует

## Исправлено

1. **RawEndpoint::stop()** — исправлен deadlock с `_clientsMutex` и порядок завершения:
   - Deadlock: `stop()` закрывал сокеты под мьютексом, `handleClients()` вызывал
     `on_client_closed()` с тем же мьютексом, `stop()` ждал join() — deadlock
   - Решение: снапшот клиентов под мьютексом, закрытие сокетов без мьютекса, join(), очистка
   - Блокирующий `acceptConnection()` заменён на `poll()` с таймаутом 1с
   - `send_hb()` больше не вызывает deadlock с `_clientsMutex`
   - Добавлена защита от повторного запуска в `start()`

## Статус сборки

| Компонент | Статус | Примечания |
|-----------|--------|------------|
| Leader (Python) | ✅ Готов | Docker build работает |
| Signaling (Python) | ✅ Готов | Docker build работает |
| Signaling (C++) | 🔄 В разработке | Фаза 0.2, selftest |
| Orb (Python) | ⚠️ Каркас | API не реализован |
| Portal (Python) | ✅ Базовый | Минимальная функциональность |
| Maintenance (C++) | ✅ Готов | Docker build работает |
| PostgreSQL | ✅ Готов | Общий для всех сервисов |

## Тестирование

| Тип тестов | Статус |
|------------|--------|
| Unit-тесты | ❌ Не реализованы |
| Интеграционные тесты | ❌ Не реализованы |
| Selftest C++ | ✅ Расширенный (SignalingEngineTest) |
| Тесты подписок | ✅ testSubscribeUnsubscribe, testOnEventCallback, testOnEventFiltering |

## Известные ограничения

1. В `requirements.txt` Orb сервиса указан `flask`, но используется `fastapi`
2. В `main.py` Orb сервиса опечатка: "Setup Signaling cluster node" вместо "Setup Orb cluster node"
3. FastAPI приложение Orb (`app.py`) содержит только импорты
4. C++ Signaling сервис работает в режиме selftest (без реального сетевого взаимодействия)
5. Скрипты `MAINTENANCE_HOST` захардкожены на `192.168.0.18`
6. `on_event()` приватный — для тестирования используется `dispatchEvent()`

---

*Последнее обновление: 30.06.2026*
