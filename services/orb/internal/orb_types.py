import os
from dataclasses import dataclass
from typing import Union
from enum import Enum
from lunaricorn.types import *

def get_required_env_vars(keys):
    missing = [key for key in keys if key not in os.environ]
    if missing:
        raise KeyError(f"Missing required environment variables: {', '.join(missing)}")
    return {key: os.environ[key] for key in keys}

@dataclass(frozen=True)
class OrbConfig:
    CLUSTER_LEADER_URL: str
    ORB_API_PORT: int
    SIGNALING_PUSH_PORT: int
    db_type: str
    db_host: str
    db_port: int
    db_user: str
    db_password: str
    db_name: str
    db_schema: str
    
    @classmethod
    def from_env(cls) -> 'OrbConfig':
        required_keys = ['CLUSTER_LEADER_URL', 'ORB_API_PORT', 'SIGNALING_PUSH_PORT',
                        "db_type", "db_host", "db_port", "db_user", "db_password", "db_name", "db_schema"]
        config_dict = get_required_env_vars(required_keys)
        config_dict['ORB_API_PORT'] = int(config_dict['ORB_API_PORT'])
        config_dict['SIGNALING_PUSH_PORT'] = int(config_dict['SIGNALING_PUSH_PORT'])
        config_dict['db_port'] = int(config_dict['db_port'])
        
        return cls(**config_dict)

def load_config() -> OrbConfig:
    return OrbConfig.from_env()


@dataclass
class OrbMetaObject(MetaObject):
    id: str

    def __post_init__(self):
        """Initialize type after object creation"""
        self.type = "@OrbMeta"

class OrbDataSybtypes(Enum):
    Json = "@json"
    Raw = "@raw"

@dataclass
class OrbDataObject(LunaObject):
    subtype: str

    def __post_init__(self):
        """Initialize type after object creation"""
        self.type = "@OrbMeta"
