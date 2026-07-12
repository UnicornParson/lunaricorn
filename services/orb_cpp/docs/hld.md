# Высокоуровневая архитектура orb_cpp

## Назначение

Сервис `orb_cpp` — C++ реализация сервиса орбитального шлюза для кластера Lunaricorn. Обеспечивает взаимодействие между узлами кластера через лидер-сервис.

## Компоненты

| Компонент | Описание |
|-----------|----------|
| `main.cpp` | Точка входа. Инициализация, подключение к лидеру, обработка сигналов |
| `oid.cpp` | Генерация уникальных Object ID (Base62 + случайный суффикс) |
| `types.h` | Типы данных (заглушка) |
| `apilib/` | Статическая библиотека `lunaricorn_api` (Leader API, Config, Maintenance, Signaling) |

## Внешние зависимости

- **Boost** (static): system, filesystem, json, beast
- **POCO** (static): Foundation, Util, Net, Data, DataPostgreSQL, JSON
- **SOCI** (static): Core, PostgreSQL
- **C++20** компилятор

## Запуск

```bash
# Нормальный режим (требуется CLUSTER_LEADER_URL)
export CLUSTER_LEADER_URL=http://leader:8001
./orb

# Тестовый режим (без лидера)
export TEST_MODE=1
./orb
```

## Сборка

Сборка производится в Docker-контейнере с использованием `lunaricorn_orb_cpp_builder`:
- `build.sh` — сборка через CMake
- `Dockerfile` — мультистейдж сборка с копированием `lunaricorn.tgz` в `apilib/`