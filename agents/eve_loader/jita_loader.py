import requests
import json
import random
import time
from typing import List, Dict, Optional, Tuple
import tqdm
import sys
import os

from sqlalchemy import create_engine
from sqlalchemy import Column, Integer, BigInteger, String, Boolean, DateTime, Numeric, Index
from sqlalchemy.orm import declarative_base
from datetime import datetime, timezone
from sqlalchemy.orm import sessionmaker
from sqlalchemy.exc import IntegrityError

Base = declarative_base()
class MarketOrderSnapshot(Base):
    """
    Модель для хранения снимков рыночных ордеров Eve Online.
    Каждая запись соответствует одному ордеру, полученному в конкретный момент сканирования.
    """
    __tablename__ = 'datalake_eve_orders'

    # Составной первичный ключ: идентификатор ордера + время сканирования
    order_id = Column(BigInteger, primary_key=True, nullable=False)
    scan_time = Column(DateTime(timezone=True), primary_key=True, nullable=False)

    # Поля, идентифицирующие контекст сканирования
    region_id = Column(Integer, nullable=False)

    # Поля ордера (соответствуют ESI-ответу)
    duration = Column(Integer)
    is_buy_order = Column(Boolean)
    issued = Column(DateTime(timezone=True))          # время выставления ордера (серверное)
    location_id = Column(BigInteger)                  # ID станции или структуры
    min_volume = Column(Integer)
    price = Column(Numeric(20, 2))                    # цена, точность 2 знака
    range = Column(String)                             # регион действия ордера ('station', 'region', ...)
    system_id = Column(Integer)
    type_id = Column(Integer)
    volume_remain = Column(Integer)
    volume_total = Column(Integer)

    __table_args__ = (
        Index('ix_market_order_snapshots_scan_time', 'scan_time'),
        Index('ix_market_order_snapshots_type_id', 'type_id'),
        Index('ix_market_order_snapshots_location_id', 'location_id'),
    )


def get_market_orders_raw(region_id=10000002, page=1, order_type='all'):
    url = f"https://esi.evetech.net/latest/markets/{region_id}/orders/"
    params = {
        'datasource': 'tranquility',
        'order_type': order_type,
        'page': page
    }
    try:
        response = requests.get(url, params=params)
        response.raise_for_status()
        raw_data = response.json()
        if 'x-pages' in response.headers:
            total_pages = int(response.headers['x-pages'])
        return raw_data, response.headers
        
    except requests.exceptions.RequestException as e:
        print(f"❌ Ошибка при выполнении запроса: {e}")
        if hasattr(e, 'response') and e.response is not None:
            print(f"   Статус ответа: {e.response.status_code}")
            print(f"   Текст ошибки: {e.response.text[:200]}")
        raise e

def get_all_market_orders(region_id: int = 10000002, order_type: str = 'all') -> Optional[List[Dict]]:
    all_orders = []
    page = 1
    total_pages = None
    raw_data, headers = get_market_orders_raw(region_id, page, order_type)
    if raw_data is None:
        print("❌ Не удалось получить первую страницу. Останавливаем сбор.")
        return None

    all_orders.extend(raw_data)
    print(f"   Страница {page}: добавлено {len(raw_data)} ордеров. Всего: {len(all_orders)}")
    if headers and 'x-pages' in headers:
        total_pages = int(headers['x-pages'])
        print(f"   Всего страниц согласно заголовкам: {total_pages}")
    else:
        total_pages = 1
        print("   Заголовок x-pages не найден, предполагаем одну страницу.")
    if total_pages > 1:
        for page in tqdm.tqdm(range(2, total_pages + 1), desc="Загрузка страниц", unit="стр"):
            time.sleep(random.uniform(1, 3))
            raw_data, headers = get_market_orders_raw(region_id, page, order_type)
            if raw_data is None:
                print(f"⚠️ Не удалось получить страницу {page}. Пропускаем её.")
                continue
            all_orders.extend(raw_data)
    return all_orders

def push_record(engine, record: dict, region_id: int) -> bool:
    required_fields = [
        'order_id', 'duration', 'is_buy_order', 'issued', 'location_id',
        'min_volume', 'price', 'range', 'system_id', 'type_id',
        'volume_remain', 'volume_total'
    ]
    if not all(field in record for field in required_fields):
        missing = [f for f in required_fields if f not in record]
        print(f"Ошибка: отсутствуют поля {missing}")
        return False
    if record['order_id'] is None:
        print("Ошибка: order_id не может быть None")
        return False
    snapshot = MarketOrderSnapshot(
        order_id=record['order_id'],
        scan_time=datetime.now(timezone.utc),          # текущее время UTC
        region_id=region_id,
        duration=record['duration'],
        is_buy_order=record['is_buy_order'],
        issued=record['issued'],                        # уже строка, ORM преобразует в datetime
        location_id=record['location_id'],
        min_volume=record['min_volume'],
        price=record['price'],
        range=record['range'],
        system_id=record['system_id'],
        type_id=record['type_id'],
        volume_remain=record['volume_remain'],
        volume_total=record['volume_total']
    )
    Session = sessionmaker(bind=engine)
    try:
        with Session() as session:
            session.add(snapshot)
            session.commit()
        return True
    except IntegrityError as e:
        # Дубликат (order_id + scan_time) или другая целостность
        print(f"Ошибка целостности при сохранении ордера {record['order_id']}: {e}")
        return False
    except Exception as e:
        print(f"Неожиданная ошибка при сохранении ордера {record['order_id']}: {e}")
        return False

def push_many(engine, records: list, region_id: int) -> int:
    required_fields = [
        'order_id', 'duration', 'is_buy_order', 'issued', 'location_id',
        'min_volume', 'price', 'range', 'system_id', 'type_id',
        'volume_remain', 'volume_total'
    ]
    valid_records = []
    for record in records:
        if not all(field in record for field in required_fields):
            missing = [f for f in required_fields if f not in record]
            print(f"⚠️ Пропуск записи (order_id={record.get('order_id')}): отсутствуют поля {missing}")
            continue
        if record['order_id'] is None:
            print("⚠️ Пропуск записи: order_id равен None")
            continue
        valid_records.append(record)

    if not valid_records:
        print("❌ Нет валидных записей для сохранения.")
        return 0

    # 2. Формирование объектов модели с общим временем сканирования
    scan_time = datetime.now(timezone.utc)
    snapshots = []
    for rec in valid_records:
        snapshots.append(MarketOrderSnapshot(
            order_id=rec['order_id'],
            scan_time=scan_time,
            region_id=region_id,
            duration=rec['duration'],
            is_buy_order=rec['is_buy_order'],
            issued=rec['issued'],
            location_id=rec['location_id'],
            min_volume=rec['min_volume'],
            price=rec['price'],
            range=rec['range'],
            system_id=rec['system_id'],
            type_id=rec['type_id'],
            volume_remain=rec['volume_remain'],
            volume_total=rec['volume_total']
        ))

    # 3. Массовая вставка
    Session = sessionmaker(bind=engine)
    try:
        with Session() as session:
            session.add_all(snapshots)
            session.commit()
        return len(snapshots)
    except IntegrityError as e:
        # Дубликат первичного ключа (order_id + scan_time) – вероятно, повторный запуск за ту же секунду
        print(f"❌ Ошибка целостности при массовом сохранении: {e}. Ни одна запись не сохранена.")
        return 0
    except Exception as e:
        print(f"❌ Неожиданная ошибка: {e}")
        return 0

def load_region(region_id, reg_name) -> bool:
    print(f"загрузка региона {reg_name}")
    DB_HOST = '192.168.0.18'
    DB_PORT = 8003
    DB_NAME = 'datalake'
    DB_USER = os.getenv('LAKE_USER')
    DB_PASSWORD = os.getenv('LAKE_PASSWORD')

    chunk_size = 1000
    if not DB_USER or not DB_PASSWORD:
        raise EnvironmentError(
            "Не заданы обязательные переменные окружения: LAKE_USER и LAKE_PASSWORD"
        )
    DATABASE_URL = f"postgresql://{DB_USER}:{DB_PASSWORD}@{DB_HOST}:{DB_PORT}/{DB_NAME}"

    engine = create_engine(DATABASE_URL)

    Base.metadata.create_all(engine)

    
    all_orders = get_all_market_orders(region_id=region_id, order_type='all')
    if not all_orders:
        print(f"⚠️ Не удалось загрузить ни одного ордера. {reg_name}")
        return False
    print(f"Итого ордеров: {len(all_orders)} для региона {reg_name}")
   
    total_records = len(all_orders)

    with tqdm.tqdm(total=total_records, desc="Сохранение ордеров {reg_name}", unit="запись") as pbar:
        for start in range(0, total_records, chunk_size):
            chunk = all_orders[start:start + chunk_size]
            saved = push_many(engine, chunk, region_id=region_id)
            pbar.update(len(chunk))
            if saved != len(chunk):
                tqdm.tqdm.write(f"⚠️ Чанк [{start}:{start+len(chunk)}]: сохранено {saved} из {len(chunk)}")
    return True
if __name__ == "__main__":
    regions = {
        "jita_region_id" : 10000002,
        "amarr_region_id" : 10000043,
        "hek_region_id" : 10000042,
        "dodixie_region_id" : 10000032,
        "rens_region_id" : 10000030
    }
    for reg_name, reg_id in regions.items():
        load_region(reg_id, reg_name)
        time.sleep(3.0)
