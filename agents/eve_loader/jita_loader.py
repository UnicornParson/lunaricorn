# Standard library
import json
import os
import random
import sys
import time
from datetime import datetime, timezone
from typing import Dict, List, Optional

# Third-party
import requests
import tqdm
from requests.adapters import HTTPAdapter
from sqlalchemy import (
    BigInteger,
    Boolean,
    Column,
    DateTime,
    ForeignKey,
    Index,
    Integer,
    Numeric,
    String,
    create_engine,
)
from sqlalchemy.dialects.postgresql import insert as pg_insert
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import declarative_base, relationship, sessionmaker
from urllib3.util.retry import Retry

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



class EveRegion(Base):
    """Справочник регионов EVE Online"""
    __tablename__ = 'dim_eve_regions'
    
    region_id = Column(Integer, primary_key=True)
    name = Column(String(100), nullable=False)
    description = Column(String(500), nullable=True)
    
    # Связи
    constellations = relationship("EveConstellation", back_populates="region")
    
    __table_args__ = (
        Index('ix_regions_name', 'name'),
    )


class EveConstellation(Base):
    """Справочник созвездий (промежуточный уровень между регионом и системой)"""
    __tablename__ = 'dim_eve_constellations'
    
    constellation_id = Column(Integer, primary_key=True)
    name = Column(String(100), nullable=False)
    region_id = Column(Integer, ForeignKey('dim_eve_regions.region_id'), nullable=False)
    
    # Связи
    region = relationship("EveRegion", back_populates="constellations")
    solar_systems = relationship("EveSolarSystem", back_populates="constellation")
    
    __table_args__ = (
        Index('ix_constellations_region_id', 'region_id'),
        Index('ix_constellations_name', 'name'),
    )


class EveSolarSystem(Base):
    """Справочник солнечных систем"""
    __tablename__ = 'dim_eve_solar_systems'
    
    system_id = Column(Integer, primary_key=True)
    name = Column(String(100), nullable=False)
    constellation_id = Column(Integer, ForeignKey('dim_eve_constellations.constellation_id'), nullable=False)
    security_status = Column(Numeric(10, 8), nullable=True)
    security_class = Column(String(10), nullable=True)
    
    # Связи
    constellation = relationship("EveConstellation", back_populates="solar_systems")
    
    __table_args__ = (
        Index('ix_solar_systems_constellation_id', 'constellation_id'),
        Index('ix_solar_systems_name', 'name'),
        Index('ix_solar_systems_security', 'security_status'),
    )

class EveCategory(Base):
    """Справочник категорий предметов"""
    __tablename__ = 'dim_eve_categories'
    
    category_id = Column(Integer, primary_key=True)
    name = Column(String(100), nullable=False)
    published = Column(Boolean, nullable=True)
    
    groups = relationship("EveGroup", back_populates="category")
    
    __table_args__ = (
        Index('ix_categories_name', 'name'),
    )


class EveGroup(Base):
    """Справочник групп предметов"""
    __tablename__ = 'dim_eve_groups'
    
    group_id = Column(Integer, primary_key=True)
    name = Column(String(100), nullable=False)
    category_id = Column(Integer, ForeignKey('dim_eve_categories.category_id'), nullable=False)
    published = Column(Boolean, nullable=True)
    
    category = relationship("EveCategory", back_populates="groups")
    types = relationship("EveType", back_populates="group")
    
    __table_args__ = (
        Index('ix_groups_category_id', 'category_id'),
        Index('ix_groups_name', 'name'),
    )


class EveType(Base):
    """Справочник типов предметов - только названия"""
    __tablename__ = 'dim_eve_types'
    
    type_id = Column(Integer, primary_key=True)
    name = Column(String(200), nullable=False)
    group_id = Column(Integer, ForeignKey('dim_eve_groups.group_id'), nullable=False)
    market_group_id = Column(Integer, nullable=True)
    description = Column(String(2000), nullable=True)
    published = Column(Boolean, nullable=True)
    
    group = relationship("EveGroup", back_populates="types")
    
    __table_args__ = (
        Index('ix_types_group_id', 'group_id'),
        Index('ix_types_name', 'name'),
        Index('ix_types_market_group_id', 'market_group_id'),
    )

def create_esi_session() -> requests.Session:
    """
    Создает сессию requests с настроенным механизмом автоматических повторов (Retries).
    Это решает проблемы с временными сбоями DNS (NameResolutionError) и обрывами связи.
    """
    session = requests.Session()
    retry_strategy = Retry(
        total=5,  # Максимум 5 попыток на уровне библиотеки
        backoff_factor=2,  # Задержки: 0s, 2s, 4s, 8s, 16s
        status_forcelist=[429, 500, 502, 503, 504],  # Повторять при серверных ошибках
        allowed_methods=["HEAD", "GET", "OPTIONS"]
    )
    adapter = HTTPAdapter(max_retries=retry_strategy)
    session.mount("http://", adapter)
    session.mount("https://", adapter)
    return session

def update_region_reference(engine, session: requests.Session, region_id: int) -> bool:
    """
    Обновляет справочник региона, созвездий и солнечных систем.
    Существующие записи перезаписываются актуальными данными из ESI.
    """
    print(f"📚 Обновление справочника региона {region_id}...")
    
    # 1. Загружаем регион
    region_url = f"https://esi.evetech.net/latest/universe/regions/{region_id}/"
    resp = session.get(region_url, timeout=30)
    if resp.status_code == 404:
        print(f"⚠️ Регион {region_id} не найден в ESI")
        return False
    resp.raise_for_status()
    region_data = resp.json()
    
    # UPSERT региона
    stmt = pg_insert(EveRegion).values(
        region_id=region_data['region_id'],
        name=region_data['name'],
        description=region_data.get('description', '')
    ).on_conflict_do_update(
        index_elements=['region_id'],
        set_={
            'name': region_data['name'],
            'description': region_data.get('description', '')
        }
    )
    with sessionmaker(bind=engine)() as db:
        db.execute(stmt)
        db.commit()
    
    # 2. Загружаем созвездия
    constellation_ids = region_data.get('constellations', [])
    constellations_data = []
    systems_data = []
    
    for const_id in tqdm.tqdm(constellation_ids, desc="Созвездия", leave=False):
        time.sleep(0.1)
        try:
            const_url = f"https://esi.evetech.net/latest/universe/constellations/{const_id}/"
            const_resp = session.get(const_url, timeout=30)
            if const_resp.status_code == 404:
                continue
            const_resp.raise_for_status()
            const_json = const_resp.json()
            
            constellations_data.append({
                'constellation_id': const_json['constellation_id'],
                'name': const_json['name'],
                'region_id': region_id
            })
            
            # 3. Загружаем солнечные системы этого созвездия
            for sys_id in const_json.get('systems', []):
                time.sleep(0.05)
                try:
                    sys_url = f"https://esi.evetech.net/latest/universe/systems/{sys_id}/"
                    sys_resp = session.get(sys_url, timeout=30)
                    if sys_resp.status_code == 404:
                        continue
                    sys_resp.raise_for_status()
                    sys_json = sys_resp.json()
                    
                    systems_data.append({
                        'system_id': sys_json['system_id'],
                        'name': sys_json['name'],
                        'constellation_id': const_id,
                        'security_status': sys_json.get('security_status'),
                        'security_class': sys_json.get('security_class')
                    })
                except Exception as e:
                    print(f"\n⚠️ Ошибка системы {sys_id}: {e}")
                    continue
        except Exception as e:
            print(f"\n⚠️ Ошибка созвездия {const_id}: {e}")
            continue
    
    # Массовый UPSERT созвездий
    if constellations_data:
        stmt = pg_insert(EveConstellation).values(constellations_data)
        stmt = stmt.on_conflict_do_update(
            index_elements=['constellation_id'],
            set_={
                'name': stmt.excluded.name,
                'region_id': stmt.excluded.region_id
            }
        )
        with sessionmaker(bind=engine)() as db:
            db.execute(stmt)
            db.commit()
        print(f"   ✅ Созвездий: {len(constellations_data)}")
    
    # Массовый UPSERT солнечных систем
    if systems_data:
        stmt = pg_insert(EveSolarSystem).values(systems_data)
        stmt = stmt.on_conflict_do_update(
            index_elements=['system_id'],
            set_={
                'name': stmt.excluded.name,
                'constellation_id': stmt.excluded.constellation_id,
                'security_status': stmt.excluded.security_status,
                'security_class': stmt.excluded.security_class
            }
        )
        with sessionmaker(bind=engine)() as db:
            db.execute(stmt)
            db.commit()
        print(f"   ✅ Солнечных систем: {len(systems_data)}")
    
    return True

def update_types_reference(engine, session: requests.Session, type_ids: List[int]) -> bool:
    """
    Обновляет справочник предметов (только названия).
    Обрабатывает только переданные type_ids (из ордеров).
    """
    if not type_ids:
        print("📚 Справочник предметов: нет новых type_id для обновления")
        return True
    
    print(f"📚 Обновление справочника предметов ({len(type_ids)} уникальных)...")
    
    categories = {}
    groups = {}
    types = {}
    
    for type_id in tqdm.tqdm(type_ids, desc="Предметы", leave=False):
        time.sleep(0.05)
        try:
            type_url = f"https://esi.evetech.net/latest/universe/types/{type_id}/"
            type_resp = session.get(type_url, timeout=30)
            if type_resp.status_code == 404:
                continue
            type_resp.raise_for_status()
            type_json = type_resp.json()
            
            types[type_id] = {
                'type_id': type_json['type_id'],
                'name': type_json['name'],
                'group_id': type_json['group_id'],
                'market_group_id': type_json.get('market_group_id'),
                'description': type_json.get('description', ''),
                'published': type_json.get('published', True)
            }
            
            grp_id = type_json['group_id']
            if grp_id not in groups:
                grp_url = f"https://esi.evetech.net/latest/universe/groups/{grp_id}/"
                grp_resp = session.get(grp_url, timeout=30)
                if grp_resp.status_code == 404:
                    continue
                grp_resp.raise_for_status()
                grp_json = grp_resp.json()
                
                groups[grp_id] = {
                    'group_id': grp_json['group_id'],
                    'name': grp_json['name'],
                    'category_id': grp_json['category_id'],
                    'published': grp_json.get('published', True)
                }
                
                cat_id = grp_json['category_id']
                if cat_id not in categories:
                    cat_url = f"https://esi.evetech.net/latest/universe/categories/{cat_id}/"
                    cat_resp = session.get(cat_url, timeout=30)
                    if cat_resp.status_code == 404:
                        continue
                    cat_resp.raise_for_status()
                    cat_json = cat_resp.json()
                    
                    categories[cat_id] = {
                        'category_id': cat_json['category_id'],
                        'name': cat_json['name'],
                        'published': cat_json.get('published', True)
                    }
        except Exception as e:
            print(f"\n⚠️ Ошибка загрузки типа {type_id}: {e}")
            continue
    
    with sessionmaker(bind=engine)() as db:
        if categories:
            stmt = pg_insert(EveCategory).values(list(categories.values()))
            stmt = stmt.on_conflict_do_update(
                index_elements=['category_id'],
                set_={
                    'name': stmt.excluded.name,
                    'published': stmt.excluded.published
                }
            )
            db.execute(stmt)
        
        if groups:
            stmt = pg_insert(EveGroup).values(list(groups.values()))
            stmt = stmt.on_conflict_do_update(
                index_elements=['group_id'],
                set_={
                    'name': stmt.excluded.name,
                    'category_id': stmt.excluded.category_id,
                    'published': stmt.excluded.published
                }
            )
            db.execute(stmt)
        
        if types:
            stmt = pg_insert(EveType).values(list(types.values()))
            stmt = stmt.on_conflict_do_update(
                index_elements=['type_id'],
                set_={
                    'name': stmt.excluded.name,
                    'group_id': stmt.excluded.group_id,
                    'market_group_id': stmt.excluded.market_group_id,
                    'description': stmt.excluded.description,
                    'published': stmt.excluded.published
                }
            )
            db.execute(stmt)
        
        db.commit()
    
    print(f"   ✅ Категорий: {len(categories)}, групп: {len(groups)}, типов: {len(types)}")
    return True

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


def load_region(region_id, reg_name) -> bool:
    print(f"\n{'='*60}")
    print(f"загрузка региона {reg_name} (ID: {region_id})")
    print('='*60)
    
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

    # Создаем сессию для справочников
    session = create_esi_session()
    
    # 1. Загружаем ордера (функция сама создает сессию внутри)
    all_orders = get_all_market_orders(region_id=region_id, order_type='all')
    if not all_orders:
        print(f"⚠️ Не удалось загрузить ни одного ордера. {reg_name}")
        return False
    print(f"Итого ордеров: {len(all_orders)} для региона {reg_name}")
    
    # 2. Извлекаем уникальные type_id из ордеров
    unique_type_ids = list({order['type_id'] for order in all_orders if order.get('type_id')})
    print(f"Уникальных предметов в ордерах: {len(unique_type_ids)}")
    if False:
        # 3. Обновляем справочник региона (созвездия + системы)
        try:
            update_region_reference(engine, session, region_id)
        except Exception as e:
            print(f"⚠️ Не удалось обновить справочник региона: {e}")
        
        # 4. Обновляем справочник предметов (только те, что есть в ордерах)
        try:
            update_types_reference(engine, session, unique_type_ids)
        except Exception as e:
            print(f"⚠️ Не удалось обновить справочник предметов: {e}")
    
    # 5. Сохраняем ордера
    total_records = len(all_orders)
    with tqdm.tqdm(total=total_records, desc=f"Сохранение ордеров {reg_name}", unit="запись") as pbar:
        for start in range(0, total_records, chunk_size):
            chunk = all_orders[start:start + chunk_size]
            saved = push_many(engine, chunk, region_id=region_id)
            pbar.update(len(chunk))
            if saved != len(chunk):
                tqdm.tqdm.write(f"⚠️ Чанк [{start}:{start+len(chunk)}]: сохранено {saved} из {len(chunk)}")
    
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