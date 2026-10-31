# EVE Online Market Orders Data Lake Loader

Скрипт для периодического сбора (снятия слепков) рыночных ордеров EVE Online через ESI API и сохранения их в базу данных PostgreSQL. Данные сохраняются в виде исторических снимков (snapshots), что позволяет анализировать изменение рынка во времени.

Также скрипт загружает и обновляет справочники (dimension tables) из EVE Online Universe API: регионы, созвездия, солнечные системы, категории/группы/типы предметов.

## 🗄️ Структура базы данных

### 📊 Таблицы фактов (Fact Tables)

#### Таблица: `datalake_eve_orders` — исторические снимки рыночных ордеров

Используется составной первичный ключ (`order_id`, `scan_time`), чтобы один и тот же ордер мог быть записан многократно в разное время сканирования.

| Имя столбца | Тип данных (SQLAlchemy / PostgreSQL) | NULL | Описание |
| :--- | :--- | :---: | :--- |
| **`order_id`** | `BigInteger` / `BIGINT` | ❌ Нет | Уникальный идентификатор ордера в EVE Online. **(Часть составного PK)** |
| **`scan_time`** | `DateTime(timezone=True)` / `TIMESTAMP WITH TIME ZONE` | ❌ Нет | Время снятия слепка (записи в БД) в формате UTC. **(Часть составного PK)** |
| **`region_id`** | `Integer` / `INTEGER` | ❌ Нет | Идентификатор региона, для которого загружаются данные (напр., `10000002` — The Forge). |
| `duration` | `Integer` / `INTEGER` | ✅ Да | Длительность ордера в днях. |
| `is_buy_order` | `Boolean` / `BOOLEAN` | ✅ Да | Флаг типа ордера: `True` — ордер на покупку (buy), `False` — на продажу (sell). |
| `issued` | `DateTime(timezone=True)` / `TIMESTAMP WITH TIME ZONE` | ✅ Да | Дата и время первоначального выставления ордера (серверное время EVE). |
| `location_id` | `BigInteger` / `BIGINT` | ✅ Да | Идентификатор станции или структуры, где размещен ордер. |
| `min_volume` | `Integer` / `INTEGER` | ✅ Да | Минимальный объем предмета для одной сделки по этому ордеру. |
| `price` | `Numeric(20, 2)` / `NUMERIC(20,2)` | ✅ Да | Цена за единицу предмета (точность до 2 знаков после запятой). |
| `range` | `String` / `VARCHAR` | ✅ Да | Дальность действия ордера (напр., `'station'`, `'system'`, `'region'`, `'1'`–`'40'`). |
| `system_id` | `Integer` / `INTEGER` | ✅ Да | Идентификатор солнечной системы, в которой находится ордер. |
| `type_id` | `Integer` / `INTEGER` | ✅ Да | Идентификатор типа предмета (typeID), который покупается или продается. |
| `volume_remain`| `Integer` / `INTEGER` | ✅ Да | Оставшийся объем ордера на момент сканирования. |
| `volume_total` | `Integer` / `INTEGER` | ✅ Да | Общий (начальный) объем ордера при его создании. |

**Индексы:**
- `ix_market_order_snapshots_scan_time` — по столбцу `scan_time` (для временных срезов).
- `ix_market_order_snapshots_type_id` — по столбцу `type_id` (для поиска истории цен).
- `ix_market_order_snapshots_location_id` — по столбцу `location_id`.

> **Примечание:** Составной первичный ключ (`order_id`, `scan_time`) автоматически создает уникальный индекс, предотвращающий дублирование записей одного и того же ордера в одну и ту же секунду времени.

### 📚 Справочники (Dimension Tables)

#### Таблица: `dim_eve_regions` — справочник регионов

| Имя столбца | Тип данных | NULL | Описание |
| :--- | :--- | :---: | :--- |
| **`region_id`** | `Integer` / `INTEGER` | ❌ Нет | Уникальный идентификатор региона. |
| `name` | `String(100)` / `VARCHAR(100)` | ❌ Нет | Название региона. |
| `description` | `String(500)` / `VARCHAR(500)` | ✅ Да | Описание региона. |

**Индекс:** `ix_regions_name` — по столбцу `name`.

---

#### Таблица: `dim_eve_constellations` — справочник созвездий

| Имя столбца | Тип данных | NULL | Описание |
| :--- | :--- | :---: | :--- |
| **`constellation_id`** | `Integer` / `INTEGER` | ❌ Нет | Уникальный идентификатор созвездия. |
| `name` | `String(100)` / `VARCHAR(100)` | ❌ Нет | Название созвездия. |
| `region_id` | `Integer` / `INTEGER` | ❌ Нет | FK → `dim_eve_regions.region_id` |

**Индексы:**
- `ix_constellations_region_id` — по `region_id`.
- `ix_constellations_name` — по `name`.

---

#### Таблица: `dim_eve_solar_systems` — справочник солнечных систем

| Имя столбца | Тип данных | NULL | Описание |
| :--- | :--- | :---: | :--- |
| **`system_id`** | `Integer` / `INTEGER` | ❌ Нет | Уникальный идентификатор системы. |
| `name` | `String(100)` / `VARCHAR(100)` | ❌ Нет | Название системы. |
| `constellation_id` | `Integer` / `INTEGER` | ❌ Нет | FK → `dim_eve_constellations.constellation_id` |
| `security_status` | `Numeric(10, 8)` / `NUMERIC(10,8)` | ✅ Да | Статус безопасности системы (0.0 — 1.0). |
| `security_class` | `String(10)` / `VARCHAR(10)` | ✅ Да | Класс безопасности (S, W1-W5, D, C, M). |

**Индексы:**
- `ix_solar_systems_constellation_id` — по `constellation_id`.
- `ix_solar_systems_name` — по `name`.
- `ix_solar_systems_security` — по `security_status`.

---

#### Таблица: `dim_eve_categories` — справочник категорий предметов

| Имя столбца | Тип данных | NULL | Описание |
| :--- | :--- | :---: | :--- |
| **`category_id`** | `Integer` / `INTEGER` | ❌ Нет | Уникальный идентификатор категории. |
| `name` | `String(100)` / `VARCHAR(100)` | ❌ Нет | Название категории. |
| `published` | `Boolean` / `BOOLEAN` | ✅ Да | Публикован ли предмет (для фильтрации). |

**Индекс:** `ix_categories_name` — по столбцу `name`.

---

#### Таблица: `dim_eve_groups` — справочник групп предметов

| Имя столбца | Тип данных | NULL | Описание |
| :--- | :--- | :---: | :--- |
| **`group_id`** | `Integer` / `INTEGER` | ❌ Нет | Уникальный идентификатор группы. |
| `name` | `String(100)` / `VARCHAR(100)` | ❌ Нет | Название группы. |
| `category_id` | `Integer` / `INTEGER` | ❌ Нет | FK → `dim_eve_categories.category_id` |
| `published` | `Boolean` / `BOOLEAN` | ✅ Да | Публикован ли предмет. |

**Индексы:**
- `ix_groups_category_id` — по `category_id`.
- `ix_groups_name` — по `name`.

---

#### Таблица: `dim_eve_types` — справочник типов предметов

| Имя столбца | Тип данных | NULL | Описание |
| :--- | :--- | :---: | :--- |
| **`type_id`** | `Integer` / `INTEGER` | ❌ Нет | Уникальный идентификатор типа предмета. |
| `name` | `String(200)` / `VARCHAR(200)` | ❌ Нет | Название предмета. |
| `group_id` | `Integer` / `INTEGER` | ❌ Нет | FK → `dim_eve_groups.group_id` |
| `market_group_id` | `Integer` / `INTEGER` | ✅ Да | ID рыноговой группы (может быть NULL). |
| `description` | `String(2000)` / `VARCHAR(2000)` | ✅ Да | Описание предмета. |
| `published` | `Boolean` / `BOOLEAN` | ✅ Да | Публикован ли предмет. |

**Индексы:**
- `ix_types_group_id` — по `group_id`.
- `ix_types_name` — по `name`.
- `ix_types_market_group_id` — по `market_group_id`.

---

## ⚙️ Требования к окружению

1. **Python 3.8+**
2. **PostgreSQL** (сервер должен быть доступен по сети)
3. Зависимости Python:
   ```bash
   pip install requests sqlalchemy psycopg2-binary tqdm
   ```

### Переменные окружения
Для подключения к базе данных скрипт требует наличия следующих переменных окружения:
- `LAKE_USER` — имя пользователя базы данных.
- `LAKE_PASSWORD` — пароль пользователя базы данных.

Пример установки в Linux/macOS:
```bash
export LAKE_USER="your_db_user"
export LAKE_PASSWORD="your_secure_password"
```

---

## 🚀 Настройка и запуск

1. Убедитесь, что база данных PostgreSQL доступна по адресу, указанному в коде (`192.168.0.18:8003`, база `datalake`). При необходимости измените эти значения в функции `load_region`.
2. Экспортируйте переменные окружения (см. выше).
3. Запустите скрипт:
   ```bash
   python jita_loader.py
   ```

### Логика работы

1. **Загрузка ордеров:**
   - Скрипт последовательно обходит предустановленные торговые хабы: Jita (10000002), Amarr (10000043), Hek (10000042), Dodixie (10000032), Rens (10000030).
   - Для каждого региона запрашиваются все страницы ордеров через ESI API с задержкой 0.3с между запросами.
   - Данные группируются в чанки по 1000 записей и сохраняются в БД с единым `scan_time` для всего чанка.
   - Ошибки целостности (`IntegrityError`) перехватываются: дубликаты игнорируются.

2. **Обновление справочников (опционально, закомментировано по умолчанию):**
   - `update_region_reference()` — загружает и обновляетRegion → Constellations → Solar Systems иерархию с помощью PostgreSQL `UPSERT` (`ON CONFLICT DO UPDATE`).
   - `update_types_reference()` — загружает только те предметы, которые встречаются в загруженных ордерах. Рекурсивно подгружает категории и группы.

3. **Устойчивость к ошибкам:**
   - `Retry` механизм с экспоненциальной задержкой (backoff_factor=2) для HTTP 429, 500-504.
   - Пулл соединений (HTTPAdapter) для переиспользования TCP-соединений.
   - Пауза 2.0с между обработкой регионов.

---

## 📊 Примеры полезных SQL-запросов

**1. Получить последнюю известную цену продажи (sell) для конкретного предмета (type_id) в Jita:**
```sql
SELECT price, volume_remain, issued
FROM datalake_eve_orders
WHERE type_id = 34 -- Например, Tritanium
  AND region_id = 10000002
  AND is_buy_order = FALSE
ORDER BY scan_time DESC, price ASC
LIMIT 1;
```

**2. Проанализировать изменение средней цены ордера во времени:**
```sql
SELECT 
    DATE(scan_time) AS scan_date,
    AVG(price) AS avg_price,
    MIN(price) AS min_price,
    MAX(price) AS max_price
FROM datalake_eve_orders
WHERE type_id = 34
  AND region_id = 10000002
  AND is_buy_order = FALSE
GROUP BY DATE(scan_time)
ORDER BY scan_date DESC;
```

**3. Найти ордеры, которые были частично выкуплены:**
```sql
SELECT order_id, type_id, volume_total, volume_remain, (volume_total - volume_remain) AS sold_volume
FROM datalake_eve_orders
WHERE volume_remain < volume_total
  AND scan_time = (SELECT MAX(scan_time) FROM datalake_eve_orders);
```

**4. Получить название предмета из справочника:**
```sql
SELECT m.type_id, t.name, t.description, c.name as category
FROM datalake_eve_orders m
JOIN dim_eve_types t ON m.type_id = t.type_id
JOIN dim_eve_groups g ON t.group_id = g.group_id
JOIN dim_eve_categories c ON g.category_id = c.category_id
WHERE m.scan_time = (SELECT MAX(scan_time) FROM datalake_eve_orders)
  AND m.type_id = 34;
```

**5. Получить информацию о системе ордера:**
```sql
SELECT m.order_id, s.name as system_name, s.security_status, s.security_class
FROM datalake_eve_orders m
JOIN dim_eve_solar_systems s ON m.system_id = s.system_id
WHERE m.scan_time = (SELECT MAX(scan_time) FROM datalake_eve_orders)
  AND m.is_buy_order = FALSE
ORDER BY s.security_status ASC
LIMIT 10;
```

---

## ⚠️ Ограничения и рекомендации

- **ESI Rate Limits**: EVE ESI имеет ограничения на количество запросов. Текущая реализация использует задержку 0.3с между запросами + Retry-механизм с backoff.
- **Время сканирования**: `scan_time` присваивается в момент формирования чанка в Python, а не в момент ответа от API.
- **Справочники**: Обновление справочников (регионы, типы) закомментировано по умолчанию (`if False:`). Для включения раскомментируйте строки 584 и 592 в `jita_loader.py`.
- Для продакшн-использования рекомендуется вынести настройки подключения к БД (`DB_HOST`, `DB_PORT`, `DB_NAME`) в переменные окружения или файл конфигурации (`.env`).