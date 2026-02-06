from sqlalchemy import create_engine, Column, BigInteger, String, Text, select
from sqlalchemy.orm import declarative_base, Session
from sqlalchemy.exc import SQLAlchemyError
from typing import List, Optional, Dict, Any


"""
CREATE TABLE public.maintenance_log
(
    "offset" bigserial NOT NULL,
    owner character varying(256) NOT NULL,
    token character varying(256) NOT NULL,
    msg text NOT NULL,
    PRIMARY KEY ("offset")
);

ALTER TABLE IF EXISTS public.maintenance_log
    OWNER to lunaricorn;
"""


Base = declarative_base()


class DbConfig:
    def __init__(self):
        self.db_type = None
        self.db_host = None
        self.db_port = None
        self.db_user = None
        self.db_password = None
        self.db_dbname = None
    def valid(self) -> bool:
        return self.db_type and self.db_host and self.db_port and self.db_user and self.db_password and self.db_dbname
    def to_str(self) -> str:
        return f"{self.db_user}@{self.db_host}:{self.db_port}/{self.db_dbname}"

class MaintenanceLogModel(Base):
    __tablename__ = 'maintenance_log'
    offset = Column('offset', BigInteger, primary_key=True, autoincrement=True)
    owner = Column(String(256), nullable=False, index=True) 
    token = Column(String(256), nullable=False, index=True)
    msg = Column(Text, nullable=False)


class Storage:
    def __init__(self, db_config):
        if not db_config.valid():
            raise ValueError("Invalid database configuration")
        
        if db_config.db_type.lower() == 'postgresql':
            connection_string = f"postgresql://{db_config.db_user}:{db_config.db_password}@{db_config.db_host}:{db_config.db_port}/{db_config.db_dbname}"
        else:
            raise ValueError(f"Unsupported database type: {db_config.db_type}")
        self.engine = create_engine(connection_string)
        self.Session = Session
        self._test_connection()
    
    def _test_connection(self):
        try:
            with self.engine.connect() as conn:
                conn.execute(select(1))
        except SQLAlchemyError as e:
            raise ConnectionError(f"Failed to connect to database: {e}")
    
    def install(self):
        try:
            Base.metadata.create_all(self.engine)
            print(f"Table '{MaintenanceLogModel.__tablename__}' created successfully")
            return True
        except SQLAlchemyError as e:
            print(f"Failed to create table: {e}")
            return False
    
    def push(self, owner: str, token: str, msg: str) -> Optional[int]:
        try:
            with self.Session(bind=self.engine) as session:
                new_record = MaintenanceLogModel(
                    owner=owner[:256],
                    token=token[:256],
                    msg=msg
                )
                
                session.add(new_record)
                session.commit()
                return new_record.offset
        except SQLAlchemyError as e:
            print(f"Failed to insert record: {e}")
            return None
    
    def pull(self, offset: int = 0, limit: Optional[int] = None) -> List[Dict[str, Any]]:
        try:
            with self.Session(bind=self.engine) as session:
                query = select(MaintenanceLogModel).where(MaintenanceLogModel.offset > offset)
                if limit is not None:
                    query = query.limit(limit)
                result = session.execute(query)
                records = result.scalars().all()
                return [
                    {
                        'offset': record.offset,
                        'owner': record.owner,
                        'token': record.token,
                        'msg': record.msg
                    }
                    for record in records
                ]
        except SQLAlchemyError as e:
            print(f"Failed to fetch records: {e}")
            return []
    
    def get_all(self) -> List[Dict[str, Any]]:
        return self.pull(offset=-1)
    
    def get_by_offset(self, offset: int) -> Optional[Dict[str, Any]]:
        try:
            with self.Session(bind=self.engine) as session:
                query = select(MaintenanceLogModel).where(MaintenanceLogModel.offset == offset)
                result = session.execute(query)
                record = result.scalar_one_or_none()
                
                if record:
                    return {
                        'offset': record.offset,
                        'owner': record.owner,
                        'token': record.token,
                        'msg': record.msg
                    }
                return None
        except SQLAlchemyError as e:
            print(f"Failed to fetch record: {e}")
            return None
    
    def count_records(self) -> int:
        try:
            with self.Session(bind=self.engine) as session:
                query = select(MaintenanceLogModel)
                result = session.execute(query)
                return len(result.fetchall())
        except SQLAlchemyError as e:
            print(f"Failed to count records: {e}")
            return 0
    
    def delete_by_offset(self, offset: int) -> bool:
        try:
            with self.Session(bind=self.engine) as session:
                query = select(MaintenanceLogModel).where(MaintenanceLogModel.offset == offset)
                result = session.execute(query)
                record = result.scalar_one_or_none()
                
                if record:
                    session.delete(record)
                    session.commit()
                    return True
                return False
        except SQLAlchemyError as e:
            print(f"Failed to delete record: {e}")
            return False