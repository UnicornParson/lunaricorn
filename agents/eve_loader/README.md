# EVE Online Market Orders Data Lake Loader

Скрипт для периодического сбора (снятия слепков) рыночных ордеров EVE Online через ESI API и сохранения их в базу данных PostgreSQL. Данные сохраняются в виде исторических снимков (snapshots), что позволяет анализировать изменение рынка во времени.

## 🗄️ Структура базы данных

Данные хранятся в таблице `datalake_eve_orders`. Используется составной первичный ключ, чтобы один и тот же ордер мог быть записан многократно в разное время сканирования.

### Таблица: `datalake_eve_orders`

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

### Индексы
Для оптимизации аналитических запросов созданы следующие индексы:
- `ix_market_order_snapshots_scan_time` — по столбцу `scan_time` (для временных срезов и исторического анализа).
- `ix_market_order_snapshots_type_id` — по столбцу `type_id` (для быстрого поиска истории цен конкретного предмета).
- `ix_market_order_snapshots_location_id` — по столбцу `location_id` (для анализа рынка на конкретных станциях/структурах).

> **Примечание:** Составной первичный ключ (`order_id`, `scan_time`) автоматически создает уникальный индекс, предотвращающий дублирование записей одного и того же ордера в одну и ту же секунду времени.

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
   python load_orders.py
   ```

### Логика работы
- Скрипт последовательно обходит предустановленные торговые хабы: Jita (The Forge), Amarr, Hek, Dodixie, Rens.
- Для каждого региона запрашиваются все страницы ордеров через ESI API с рандомизированной задержкой (1–3 секунды) для соблюдения лимитов.
- Данные группируются в чанки по 1000 записей и сохраняются в БД с единым `scan_time` для всего чанка.
- Ошибки целостности (`IntegrityError`) перехватываются: дубликаты (повторный запуск в ту же секунду) игнорируются, процесс продолжается.

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

**3. Найти ордеры, которые были частично выкуплены (остаток меньше общего объема):**
```sql
SELECT order_id, type_id, volume_total, volume_remain, (volume_total - volume_remain) AS sold_volume
FROM datalake_eve_orders
WHERE volume_remain < volume_total
  AND scan_time = (SELECT MAX(scan_time) FROM datalake_eve_orders);
```

---

## ⚠️ Ограничения и рекомендации
- **ESI Rate Limits**: EVE ESI имеет ограничения на количество запросов. Текущая реализация использует `time.sleep(random.uniform(1, 3))`, что безопасно для периодического сбора, но может быть медленным для полных дампов всех регионов.
- **Время сканирования**: `scan_time` присваивается в момент формирования чанка в Python, а не в момент ответа от API. Разница составляет доли секунды, что приемлемо для большинства аналитических задач.
- Для продакшн-использования рекомендуется вынести настройки подключения к БД (`DB_HOST`, `DB_PORT`, `DB_NAME`) в переменные окружения или файл конфигурации (`.env`).
```
