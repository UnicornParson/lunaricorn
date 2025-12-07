import os
from dataclasses import dataclass, field
from typing import Union
from enum import Enum
from lunaricorn.types import *
from lunaricorn.utils.db_manager import *
from datetime import datetime, timezone
import json

def get_required_env_vars(keys):
    missing = [key for key in keys if key not in os.environ]
    if missing:
        raise KeyError(f"Missing required environment variables: {', '.join(missing)}")
    return {key: os.environ[key] for key in keys}

@dataclass(frozen=True)
class OrbConfig:
    CLUSTER_LEADER_URL: str
    ORB_API_PORT: int
    SIGNALING_HOST: str
    SIGNALING_REQ: int
    SIGNALING_PUB: int
    SIGNALING_API: int
    db_type: str
    db_host: str
    db_port: int
    db_user: str
    db_password: str
    db_name: str
    db_schema: str

    @classmethod
    def from_env(cls) -> 'OrbConfig':
        required_keys = ['CLUSTER_LEADER_URL', 'ORB_API_PORT', 'SIGNALING_REQ', 'SIGNALING_PUB', 'SIGNALING_API', 'SIGNALING_HOST',
                        "db_type", "db_host", "db_port", "db_user", "db_password", "db_name", "db_schema"]
        config_dict = get_required_env_vars(required_keys)
        config_dict['ORB_API_PORT'] = int(config_dict['ORB_API_PORT'])
        config_dict['SIGNALING_HOST'] = str(config_dict['SIGNALING_HOST'])
        config_dict['SIGNALING_REQ'] = int(config_dict['SIGNALING_REQ'])
        config_dict['SIGNALING_PUB'] = int(config_dict['SIGNALING_PUB'])
        config_dict['SIGNALING_API'] = int(config_dict['SIGNALING_API'])
        config_dict['db_port'] = int(config_dict['db_port'])

        return cls(**config_dict)

    def valid(self) -> bool:
        return self.db_type and self.db_host and self.db_port and self.db_user and self.db_password and self.db_name and self.SIGNALING_HOST and self.SIGNALING_REQ and self.SIGNALING_PUB and self.SIGNALING_API

    def create_db_config(self) -> DbConfig:
        db_config = DbConfig()
        db_config.db_type = self.db_type
        db_config.db_host = self.db_host
        db_config.db_port = self.db_port
        db_config.db_user = self.db_user
        db_config.db_password = self.db_password
        db_config.db_dbname = self.db_name
        return db_config

def load_config() -> OrbConfig:
    return OrbConfig.from_env()


@dataclass
class OrbMetaObject(MetaObject):
    id: int = 0
    flags: list[str] = field(default_factory=list)
    ctime: datetime = field(default_factory=lambda: utime())
    def to_record(self):
        return {
            'id': self.id,
            'u': str(self.u),
            'ctime': str(self.ctime.isoformat()) if self.ctime else utime_s(),
            'type': self.type.value,
            'handle': str(self.handle) if self.handle else None,
            'flags': json.dumps(self.flags) if isinstance(self.flags, (list, dict)) else '[]',
        }
    def __post_init__(self):
        """Initialize type after object creation"""
        self.type = "@OrbMeta"

class OrbDataSybtypes(Enum):
    Json = "@json"
    Raw = "@raw"

@dataclass
class OrbDataObject(LunaObject):
    src: Optional[str]
    data: Optional[dict]
    chain_left: Optional[Any] = None
    chain_right: Optional[Any] = None
    parent: Optional[Any] = None
    ctime: datetime = field(default_factory=lambda: utime())
    flags: list[str] = field(default_factory=list)
    subtype: str = OrbDataSybtypes.Json

    def __post_init__(self):
        """Initialize type after object creation"""
        self.type = "@OrbData"
        self.chain_left = None
        self.chain_right = None
        self.parent = None

    def to_record(self):
        return {
            'u': str(self.u),
            'ctime': str( self.ctime.isoformat() if self.ctime else utime_s() ),
            'data_type': self.subtype,
            'chain_left': str(self.chain_left) if self.chain_left else None,
            'chain_right': str(self.chain_right) if self.chain_right else None,
            'parent': str(self.parent) if self.parent else None,
            'flags': json.dumps(self.flags) if isinstance(self.flags, (list, dict)) else '[]',
            'src': self.src,
            'data': json.dumps(self.data) if isinstance(self.data, dict) else '{}'
        }
