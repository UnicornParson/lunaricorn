# Архитектура проекта Lunaricorn

## Обзор

**Lunaricorn** — это самоорганизующаяся кластерная микросервисная платформа. Название происходит от сочетания "lunar" (лунный) и "unicorn" (единорог) — метафора магического ядра, где устройства и сервисы объединяются под заклинанием единорога.

Проект позволяет разворачивать распределённую систему из независимых микросервисов с общей инфраструктурой.

## Принцип работы

1. **Leader** выбирает и управляет главным узлом кластера
2. **Сервисы** регистрируются у Leader и получают информацию о других узлах
3. **Signaling** обеспечивает межсервисное взаимодействие через ZeroMQ и HTTP
4. **Maintenance** собирает и обрабатывает логи
5. **Orb** предоставляет орбитальные функции и файловое хранилище
6. **Portal** даёт веб-интерфейс управления

---

## Общая диаграмма архитектуры

```
                    ┌─────────────────┐
                    │   Portal        │
                    │   (порт 8002)    │
                    └────────┬────────┘
                             │
                    ┌────────▼────────┐
                    │   Leader        │
                    │   (порт 8001)    │
                    │  Кластерный     │
                    │  менеджер        │
                    └────────┬────────┘
                             │
          ┌──────────────────┼──────────────────┐
          │                  │                  │
   ┌──────▼──────┐   ┌──────▼──────┐   ┌──────▼──────┐
   │ Signaling   │   │   Orb       │   │Maintenance  │
   │ (Python)    │   │             │   │             │
   │ 5555/5556/  │   │ 8080/50051  │   │ (порт       │
   │  5557)      │   │             │   │  MAINTENANCE│
   └──────┬──────┘   └──────┬──────┘   │  _API_PORT) │
          │                  │           └──────┬──────┘
          │                  │                  │
   ┌──────▼──────────────────▼──────────────────▼──────┐
   │              PostgreSQL (порт 5432)                 │
   │                    lunaricorn                       │
   └────────────────────────────────────────────────────┘
```

---

## Сервисы

### 1. Leader Service

**Расположение:** `services/leader/`

**Назначение:** Управление кластером, выбор лидера, регистрация узлов.

**Технологии:**
- Python + FastAPI
- PostgreSQL (хранение состояния кластера)

**Ключевые файлы:**
- `main.py` — точка входа
- `app.py` — FastAPI приложение
- `Dockerfile` — образ контейнера

**API:**
- `GET /health` — health check
- Регистрация узлов кластера

**Порты:**
- `8001` → `8000` (внутри контейнера)

**Зависимости:**
- PostgreSQL (pg)
- Maintenance сервис

---

### 2. Signaling Service (Python)

**Расположение:** `services/signaling/`

**Назначение:** Межсервисное взаимодействие через publish/subscribe и request/reply паттерны.

**Технологии:**
- Python + ZeroMQ
- FastAPI
- PostgreSQL

**Ключевые файлы:**
- `main.py` — точка входа
- `zmq_server.py` — ZeroMQ сервер
- `api_server.py` — HTTP API
- `logger_config.py` — логирование
- `internal/` — внутренние модули
- `cfg/` — конфигурация

**Порты:**
- `5555` — REQ-REP (запрос-ответ)
- `5556` — PUB-SUB (публикация-подписка)
- `5557` — HTTP API

**Зависимости:**
- PostgreSQL
- Leader сервис

---

### 3. Signaling Service (C++)

**Расположение:** `services/signaling_cpp/`

**Назначение:** Высокопроизводительная реализация сигнализационного сервиса.

**Технологии:**
- C++ (современный стандарт)
- Poco Framework
- PostgreSQL (через MessageStorage)

**Ключевые файлы:**
- `main.cpp` — точка входа
- `signaling_engine.h/cpp` — ядро обработки событий
- `message_storage.h/cpp` — слой хранения
- `event_data_extended_type_handler.h/cpp` — обработчик типов
- `raw_endpoint.h/cpp` — сырой API
- `signaling_engine_test.h/cpp` — selftest
- `config.h` — конфигурация
- `stdafx.h` — precompiled header

**Архитектура C++ компонента:**

```
main.cpp
    │
    ├─ MLog (система логирования)
    │
    ├─ SignalingEngine
    │   │
    │   ├─ MessageStorage
    │   │   └─ PostgreSQL
    │   │
    │   └─ EventDataExtendedTypeHandler
    │
    └─ SignalingEngineTest (selftest)
```

**Порты:**
- Нет активных сетевых портов (selftest режим)

**Конфигурация:**
Загружается из переменных окружения через `loadConfigFromEnvironment()`.

**Зависимости:**
- PostgreSQL
- Maintenance сервис (для логов)

---

### 4. Orb Service

**Расположение:** `services/orb/`

**Назначение:** Орбитальный сервис — хранение данных, файловое хранилище, REST/GRPC API.

**Технологии:**
- Python + FastAPI
- gRPC (grpc_app.py)
- PostgreSQL

**Ключевые файлы:**
- `main.py` — точка входа, регистрация узла
- `app.py` — FastAPI приложение (каркас)
- `rest_app.py` — REST API
- `grpc_app.py` — gRPC API
- `node_config.py` — регистрация в кластере
- `logger_config.py` — логирование
- `internal/orb_types.py` — типы и конфигурация

**Структура:**
```
orb/
├── main.py
├── app.py
├── rest_app.py
├── grpc_app.py
├── node_config.py
├── logger_config.py
├── internal/
│   └── orb_types.py
├── Dockerfile
├── run.sh
└── requirements.txt
```

**Порты:**
- `${ORB_API_PORT}` → `50051` (API)
- `${ORB_REST_PORT}` → `8080` (REST)

**Вolumes:**
- `${LUNARICORN_HOME}/orb_data` — данные
- `${LUNARICORN_HOME}/orb_storage` — файловое хранилище

**Зависимости:**
- PostgreSQL
- Leader сервис
- Signaling сервис

---

### 5. Portal Service

**Расположение:** `services/portal/`

**Назначение:** Веб-портал управления кластером.

**Технологии:**
- Python + FastAPI
- Статические файлы

**Ключевые файлы:**
- `main.py` — точка входа
- `app.py` — FastAPI приложение
- `static/` — статические файлы
- `cfg/` — конфигурация
- `api/` — API клиенты

**Порты:**
- `8002` → `8000` (внутри контейнера)

**Зависимости:**
- Leader сервис
- Maintenance сервис

---

### 6. Maintenance Service

**Расположение:** `services/maintenance/`

**Назначение:** Сбор, хранение и обработка логов всех сервисов.

**Технологии:**
- C++ (Poco Framework)
- Многопоточность (WORKERS=4)

**Ключевые файлы:**
- `app/` — исходный код
- `api.md` — документация API
- `LogCollectorClient` — клиент для сбора логов

**API:**
- `GET /health` — health check
- `POST /log` — отправка лога
- `GET /log/pull` — получение логов
- `GET /log/download` — скачивание логов

**Порты:**
- `${MAINTENANCE_API_PORT}` → `8000` (внутри контейнера)

**Конфигурация:**
- `WORKERS=4` — количество рабочих потоков

**Зависимости:**
- PostgreSQL

---

## Общие библиотеки

### C++ Library (`lunaricorn/cpp/`)

**Назначение:** Общие утилиты для всех C++ сервисов.

**Компоненты:**

#### MLog — Система логирования
```cpp
// Использование
MLOG("Форматированное сообщение {}", value);
MLOG_D("Debug сообщение {}", value);
MLOG_W("Warning сообщение {}", value);
MLOG_E("Error сообщение {}", value);
MBUG("Баг: {}", "описание");
MBUG_IF(condition, "Если условие true, логируем баг");
```

**Параметры:**
- `MLog::owner` — имя сервиса
- `MLog::token` — идентификатор экземпляра
- `MLog::is_stub` — режим stub

#### LogCollectorClient — Клиент сбора логов
```cpp
// Singleton
LogCollectorClient& client = LogCollectorClient::instance();

// Методы
client.get_status();                    // Получить статус
client.health_check();                   // Health check
client.send_log(owner, token, msg, type); // Отправить лог
client.send_logs_batch(logs);            // Пакетная отправка
client.pull_logs(offset);                // Получить логи
client.download_logs_plain(offset);      // Скачать логи
```

**Конфигурация:**
- `base_url_` — URL Maintenance сервиса
- `timeout_` — таймаут (10 сек)
- `max_retries_` — макс. попыток (3)
- `retry_delay_` — задержка между попытками (100 мс)

#### DbConfig — Конфигурация БД
```cpp
struct DbConfig {
    std::string type;      // "postgresql"
    std::string host;      // "lunaricorn-pg"
    int port;              // 5432
    std::string user;      // "lunaricorn"
    std::string password;  // "${LUNARICORN_PASSWORD}"
    std::string name;      // "lunaricorn"
    std::string schema;    // "lunaricorn"
};
```

---

### Python Library (`lunaricorn/`)

**Структура:**
```
lunaricorn/
├── __init__.py
├── pyproject.toml
├── requirements.txt
├── api/              # API клиенты
│   ├── leader/       # Клиент Leader API
│   └── signaling/    # Клиент Signaling API
├── cpp/              # C++ интеграция
├── data/             # Работа с данными
├── net/              # Сетевые утилиты
├── types/            # Общие типы
└── utils/            # Утилиты
```

---

## Взаимодействие сервисов

### Регистрация узла в кластере

```
Сервис стартует
    │
    ▼
Проверка Leader API
    │ (retry с интервалом 5 сек)
    ▼
POST /register {node_name, node_key, node_type}
    │
    ▼
Сервис зарегистрирован в кластере
```

### Потоки данных

```
Клиент → Signaling (REQ-REP 5555)
    │
    ├──→ Обработка SignalingEngine
    │       │
    │       ├──→ Запись в PostgreSQL (MessageStorage)
    │       │
    │       └──→ Публикация через PUB-SUB (5556)
    │
    └──→ Отправка логов в Maintenance
```

---

## Сеть и порты

### Внешние порты

| Сервис | Порт | Назначение |
|--------|------|------------|
| Leader | 8001 | HTTP API |
| Portal | 8002 | Веб-портал |
| Maintenance | ${MAINTENANCE_API_PORT} | API логов |
| Orb API | ${ORB_API_PORT} | API сервиса |
| Orb REST | ${ORB_REST_PORT} | REST API |
| Signaling REQ | 5555 | ZeroMQ REQ-REP |
| Signaling PUB | 5556 | ZeroMQ PUB-SUB |
| Signaling API | 5557 | HTTP API |
| PostgreSQL | 5432 | База данных |

### Внутренняя сеть

Все сервисы подключены к сети `lunaricorn-network` (Docker bridge):
```
lunaricorn-network (bridge)
├── lunaricorn-leader
├── lunaricorn-portal
├── lunaricorn-maintenance
├── lunaricorn-orb
├── lunaricorn-signaling
└── lunaricorn-pg
```

---

## Конфигурация

### Переменные окружения

| Переменная | Описание | По умолчанию |
|-----------|----------|--------------|
| `PG_PORT` | Порт PostgreSQL | 5432 |
| `PG_ROOT_PASSWORD` | Пароль postgres | - |
| `LUNARICORN_PASSWORD` | Пароль lunaricorn | - |
| `MAINTENANCE_API_PORT` | Порт Maintenance API | - |
| `ORB_API_PORT` | Порт Orb API | - |
| `ORB_REST_PORT` | Порт Orb REST | - |
| `CLUSTER_LEADER_URL` | URL Leader API | http://leader:8000/ |
| `LUNARICORN_HOME` | Домашняя директория | - |

### Файлы конфигурации

- `.env` — переменные окружения docker-compose
- `.env.example` — пример конфигурации
- `.native_env` — локальная конфигурация (не в репозитории)
- `.native_env_example` — пример native конфигурации

---

## Deployment

### Docker Compose

**Скрипты запуска:**
```bash
# Запуск всего кластера
./services/up.sh

# Запуск отдельных сервисов
./services/leader_up.sh
./services/signaling_up.sh
./services/orb_up.sh
./services/portal_up.sh

# Остановка
./services/down.sh
```

### Структура данных

| Путь | Назначение |
|------|------------|
| ${LUNARICORN_HOME}/pg_data | PostgreSQL данные |
| ${LUNARICORN_HOME}/pg_log | PostgreSQL логи |
| ${LUNARICORN_HOME}/leader_data | Данные Leader |
| ${LUNARICORN_HOME}/portal_data | Данные Portal |
| ${LUNARICORN_HOME}/orb_data | Данные Orb |
| ${LUNARICORN_HOME}/orb_storage | Файловое хранилище Orb |
| ${LUNARICORN_HOME}/signaling_data | Данные Signaling |

---

## Зависимости между сервисами

```
Leader ← PostgreSQL, Maintenance
Portal ← Leader, Maintenance
Maintenance ← PostgreSQL
Orb ← PostgreSQL, Leader, Signaling
Signaling ← PostgreSQL, Leader
```

---

## Безопасность

### Пароли
- `PG_ROOT_PASSWORD` — суперпользователь PostgreSQL
- `LUNARICORN_PASSWORD` — сервисный пользователь

### Сеть
- Все сервисы в изолированной Docker сети
- Порты экспонируются только при необходимости

### Логирование
- Все сервисы отправляют логи в Maintenance
- Ротация логов (100MB, до 10 файлов)
- Аудит через MLog систему

---

## Планы развития архитектуры

1. Добавить Kubernetes манифесты
2. Добавить систему мониторинга (Prometheus + Grafana)
3. Реализовать схему БД и миграции
4. Добавить API Gateway
5. Реализовать Service Discovery
6. Добавить кэширование (Redis)

---

*Последнее обновление: 28.06.2026*