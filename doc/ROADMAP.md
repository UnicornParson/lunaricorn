# Дорожная карта проекта Lunaricorn

## Ближайшие задачи (Next)

### Фаза 1: Базовая функциональность

- [ ] Реализовать схему базы данных и миграции
  - Определить ER-диаграмму
  - Создать SQL миграции для всех сервисов
  - Добавить механизм применения миграций
  - **Причина:** PROBLEM-006 (Critical)

- [ ] Завершить Signaling C++ сервис
  - Раскомментировать и доработать сетевой код
  - Добавить TCP endpoint для обработки запросов
  - Настроить взаимодействие с Leader
  - **Причина:** PROBLEM-004 (High)

- [ ] Реализовать Orb API
  - Инициализировать FastAPI приложение
  - Реализовать базовые маршруты
  - Добавить бизнес-логику
  - **Причина:** PROBLEM-003 (High)

- [ ] Исправить MAINTENANCE_HOST
  - Заменить захардкоженный IP на переменную окружения
  - **Причина:** PROBLEM-005 (High)

- [ ] Добавить интеграционные тесты
  - pytest для Python сервисов
  - Google Test для C++ компонентов
  - **Причина:** PROBLEM-007 (High)

### Фаза 2: Улучшение качества

- [ ] Исправить requirements.txt Orb (flask → fastapi)
  - **Причина:** PROBLEM-001 (Medium)

- [ ] Исправить опечатку в main.py Orb
  - **Причина:** PROBLEM-002 (Low)

- [ ] Добавить OpenAPI спецификацию
  - Swagger UI для FastAPI
  - Postman коллекции
  - **Причина:** PROBLEM-008 (Medium)

- [ ] Реализовать файловое хранилище Orb
  - CRUD операции для файлов
  - Поиск и фильтрация
  - **Причина:** PROBLEM-009 (Medium)

- [ ] Добавить систему мониторинга
  - Prometheus metrics
  - Grafana dashboards
  - **Причина:** PROBLEM-011 (Medium)

---

## Среднесрочные задачи (Later)

### Фаза 3: Масштабирование

- [ ] Kubernetes deployment
  - Deployment манифесты для всех сервисов
  - Service и Ingress ресурсы
  - Helm chart
  - ConfigMap и Secret
  - **Причина:** PROBLEM-010 (Low)

- [ ] API Gateway
  - Добавить единую точку входа
  - Rate limiting
  - Authentication

- [ ] Service Discovery
  - Улучшенная регистрация узлов
  - Health monitoring
  - Load balancing

- [ ] Кэширование
  - Redis для кэширования
  - Session storage

---

## Долгосрочные задачи (Future)

### Фаза 4: Расширение функциональности

- [ ] Расширение Orb сервиса
  - Полная реализация бизнес-логики
  - GRPC API доработка
  - Файловый менеджер

- [ ] Улучшение Signaling
  - WebSocket поддержка
  - Real-time уведомления
  - Message persistence

- [ ] Портал
  - Веб-интерфейс управления
  - Дашборды
  - Управление кластером

### Фаза 5: Production readiness

- [ ] CI/CD пайплайн
  - Автоматическая сборка
  - Тестирование
  - Деплой

- [ ] Security hardening
  - TLS/mTLS
  - RBAC
  - Audit logging

- [ ] Disaster recovery
  - Backup стратегии
  - Failover
  - Recovery procedures

---

## Технические долги

### Высокий приоритет

1. **PROBLEM-006** — Схема БД и миграции
2. **PROBLEM-004** — C++ Signaling в selftest режиме
3. **PROBLEM-003** — Orb API не реализован
4. **PROBLEM-005** — Захардкоженный MAINTENANCE_HOST
5. **PROBLEM-007** — Отсутствие тестов

### Средний приоритет

1. **PROBLEM-001** — flask в requirements.txt
2. **PROBLEM-008** — Нет OpenAPI спецификации
3. **PROBLEM-009** — Файловое хранилище не работает
4. **PROBLEM-011** — Нет мониторинга

### Низкий приоритет

1. **PROBLEM-002** — Опечатка в сообщении
2. **PROBLEM-010** — Нет Kubernetes манифестов
3. **PROBLEM-012** — Порт без значения по умолчанию

---

## Зависимости между задачами

```
Фаза 1
├── Схема БД (PROBLEM-006)
│   └──→ Интеграционные тесты (PROBLEM-007)
│   └──→ Orb API (PROBLEM-003)
│   └──→ Signaling C++ (PROBLEM-004)
├── Signaling C++ (PROBLEM-004)
├── Orb API (PROBLEM-003)
├── MAINTENANCE_HOST (PROBLEM-005)
└── Интеграционные тесты (PROBLEM-007)

Фаза 2
├── Исправить requirements.txt (PROBLEM-001)
├── Исправить опечатку (PROBLEM-002)
├── OpenAPI спецификация (PROBLEM-008)
├── Файловое хранилище (PROBLEM-009)
└── Мониторинг (PROBLEM-011)

Фаза 3
├── Kubernetes (PROBLEM-010)
├── API Gateway
├── Service Discovery
└── Кэширование

Фаза 4+
└── Расширение функциональности
```

---

## Критический путь

```
Схема БД → Orb API → Интеграционные тесты → OpenAPI → Мониторинг → K8s
    │
    └──→ Signaling C++ → Интеграционные тесты
```

---

## Версионирование

### Версия 0.3 (Next MVP)
- Схема БД и миграции
- Рабочий Signaling C++
- Базовый Orb API
- Интеграционные тесты

### Версия 0.4 (Quality)
- OpenAPI спецификация
- Файловое хранилище
- Мониторинг
- Исправление всех известных проблем

### Версия 1.0 (Release)
- Kubernetes deployment
- Production readiness
- Полная документация
- Security hardening

---

*Последнее обновление: 28.06.2026*