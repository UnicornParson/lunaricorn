import requests
from requests.adapters import HTTPAdapter
from urllib3.util.retry import Retry
import json
import time
from typing import List, Dict, Optional
import tqdm
import sys
import os

from sqlalchemy import create_engine, Column, Integer, BigInteger, String, Boolean, DateTime, Numeric, Index
from sqlalchemy.orm import declarative_base, sessionmaker
from sqlalchemy.exc import IntegrityError
from datetime import datetime, timezone

Base = declarative_base()

class MarketOrderSnapshot(Base):
    __tablename__ = 'datalake_eve_orders'

    order_id = Column(BigInteger, primary_key=True, nullable=False)
    scan_time = Column(DateTime(timezone=True), primary_key=True, nullable=False)
    region_id = Column(Integer, nullable=False)
    duration = Column(Integer)
    is_buy_order = Column(Boolean)
    issued = Column(DateTime(timezone=True))
    location_id = Column(BigInteger)
    min_volume = Column(Integer)
    price = Column(Numeric(20, 2))
    range = Column(String)
    system_id = Column(Integer)
    type_id = Column(Integer)
    volume_remain = Column(Integer)
    volume_total = Column(Integer)

    __table_args__ = (
        Index('ix_market_order_snapshots_scan_time', 'scan_time'),
        Index('ix_market_order_snapshots_type_id', 'type_id'),
        Index('ix_market_order_snapshots_location_id', 'location_id'),
    )


def get_robust_session() -> requests.Session:
    """
    Создает сессию requests с автоматическими повторными попытками и пулом соединений.
    Это предотвращает ошибки DNS-резолвинга при частых запросах.
    """
    session = requests.Session()
    
    # Стратегия повторных попыток:
    # total=5: максимум 5 попыток на один запрос
    # backoff_factor=2: задержка между попытками: 2с, 4с, 8с, 16с
    # status_forcelist: повторять при этих HTTP-кодах (включая 429 Too Many Requests)
    retry_strategy = Retry(
        total=5,
        backoff_factor=2,
        status_forcelist=[429, 500, 502, 503, 504],
        allowed_methods=["GET"]
    )
    
    # Настройка пула соединений (Keep-Alive)
    adapter = HTTPAdapter(
        max_retries=retry_strategy, 
        pool_connections=20, 
        pool_maxsize=20
    )
    session.mount("https://", adapter)
    session.mount("http://", adapter)
    
    # ESI рекомендует указывать User-Agent и контакты
    session.headers.update({
        "User-Agent": "EVE_Market_Data_Laker/1.0 (contact: your_email@example.com)",
        "Accept": "application/json"
    })
    
    return session


def get_market_orders_raw(session: requests.Session, region_id: int, page: int, order_type: str = 'all'):
    url = f"https://esi.evetech.net/latest/markets/{region_id}/orders/"
    params = {
        'datasource': 'tranquility',
        'order_type': order_type,
        'page': page
    }
    try:
        response = session.get(url, params=params, timeout=15) # Добавлен таймаут 15 секунд
        response.raise_for_status()
        raw_data = response.json()
        total_pages = int(response.headers.get('x-pages', 1))
        return raw_data, total_pages
        
    except requests.exceptions.RequestException as e:
        print(f"\n❌ Ошибка при загрузке страницы {page} региона {region_id}: {e}")
        return None, None


def get_all_market_orders(region_id: int, order_type: str = 'all') -> Optional[List[Dict]]:
    all_orders = []
    page = 1
    
    # Создаем одну сессию на весь процесс загрузки региона
    session = get_robust_session()
    
    print(f"   Загрузка страницы {page}...")
    raw_data, total_pages = get_market_orders_raw(session, region_id, page, order_type)
    
    if raw_data is None:
        print("❌ Не удалось получить первую страницу. Останавливаем сбор.")
        return None

    all_orders.extend(raw_data)
    print(f"   Страница {page}: добавлено {len(raw_data)} ордеров. Всего страниц: {total_pages}")
    
    if total_pages > 1:
        # Небольшая базовая задержка, чтобы не спамить API, но быстрее чем 1-3 секунды
        # Retry-механизм сам добавит задержку при ошибках
        for page in tqdm.tqdm(range(2, total_pages + 1), desc=f"Загрузка региона {region_id}", unit="стр"):
            time.sleep(0.3) # 300 мс между успешными запросами (ESI позволяет до 100-200 запросов в секунду, но мы вежливы)
            
            raw_data, _ = get_market_orders_raw(session, region_id, page, order_type)
            if raw_data is None:
                print(f"\n⚠️ Не удалось получить страницу {page} после всех попыток. Пропускаем её.")
                continue
            
            all_orders.extend(raw_data)
            
    # Закрываем сессию для освобождения ресурсов
    session.close()
    return all_orders


def push_many(engine, records: list, region_id: int) -> int:
    required_fields = [
        'order_id', 'duration', 'is_buy_order', 'issued', 'location_id',
        'min_volume', 'price', 'range', 'system_id', 'type_id',
        'volume_remain', 'volume_total'
    ]
    valid_records = []
    for record in records:
        if not all(field in record for field in required_fields):
            continue
        if record['order_id'] is None:
            continue
        valid_records.append(record)

    if not valid_records:
        return 0

    scan_time = datetime.now(timezone.utc)
    
    # Оптимизация: подготовка словарей для bulk_insert (работает быстрее, чем add_all)
    snapshots_data = []
    for rec in valid_records:
        snapshots_data.append({
            'order_id': rec['order_id'],
            'scan_time': scan_time,
            'region_id': region_id,
            'duration': rec['duration'],
            'is_buy_order': rec['is_buy_order'],
            'issued': rec['issued'],
            'location_id': rec['location_id'],
            'min_volume': rec['min_volume'],
            'price': rec['price'],
            'range': rec['range'],
            'system_id': rec['system_id'],
            'type_id': rec['type_id'],
            'volume_remain': rec['volume_remain'],
            'volume_total': rec['volume_total']
        })

    Session = sessionmaker(bind=engine)
    try:
        with Session() as session:
            # Используем bulk_insert_mappings для значительного ускорения записи
            session.bulk_insert_mappings(MarketOrderSnapshot, snapshots_data)
            session.commit()
        return len(snapshots_data)
    except IntegrityError as e:
        session.rollback()
        # Игнорируем дубликаты, так как это нормальная ситуация при повторных запусках
        return 0
    except Exception as e:
        session.rollback()
        print(f"❌ Неожиданная ошибка БД: {e}")
        return 0


def load_region(region_id: int, reg_name: str) -> bool:
    print(f"\n=== Начало загрузки региона: {reg_name} (ID: {region_id}) ===")
    
    DB_HOST = os.getenv('DB_HOST', '192.168.0.18')
    DB_PORT = os.getenv('DB_PORT', '8003')
    DB_NAME = os.getenv('DB_NAME', 'datalake')
    DB_USER = os.getenv('LAKE_USER')
    DB_PASSWORD = os.getenv('LAKE_PASSWORD')

    if not DB_USER or not DB_PASSWORD:
        raise EnvironmentError("Не заданы обязательные переменные окружения: LAKE_USER и LAKE_PASSWORD")
        
    DATABASE_URL = f"postgresql://{DB_USER}:{DB_PASSWORD}@{DB_HOST}:{DB_PORT}/{DB_NAME}"
    engine = create_engine(DATABASE_URL, pool_pre_ping=True) # pool_pre_ping проверяет соединение перед использованием

    Base.metadata.create_all(engine)
    
    all_orders = get_all_market_orders(region_id=region_id, order_type='all')
    if not all_orders:
        print(f"⚠️ Не удалось загрузить ни одного ордера для {reg_name}")
        return False
        
    print(f"Итого получено ордеров: {len(all_orders)}")
   
    chunk_size = 2000 # Увеличили чанк для ускорения записи
    total_records = len(all_orders)
    saved_total = 0

    with tqdm.tqdm(total=total_records, desc=f"Сохранение в БД [{reg_name}]", unit="запись") as pbar:
        for start in range(0, total_records, chunk_size):
            chunk = all_orders[start:start + chunk_size]
            saved = push_many(engine, chunk, region_id=region_id)
            saved_total += saved
            pbar.update(len(chunk))
            
    print(f"✅ Регион {reg_name} завершен. Сохранено записей: {saved_total} из {total_records}")
    return True


if __name__ == "__main__":
    regions = {
        "jita": 10000002,
        "amarr": 10000043,
        "hek": 10000042,
        "dodixie": 10000032,
        "rens": 10000030
    }
    
    for reg_name, reg_id in regions.items():
        try:
            load_region(reg_id, reg_name)
            time.sleep(2.0) # Пауза между регионами
        except Exception as e:
            print(f"❌ Критическая ошибка при загрузке региона {reg_name}: {e}")
            # Продолжаем загрузку следующего региона, не прерывая весь джоб Jenkins
            
    print("\n🎉 Все регионы обработаны.")