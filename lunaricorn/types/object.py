from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Optional
import sys
import uuid
  
from datetime import datetime, timezone
class BaseObjectType(Enum):
    Base = "@base"
    Meta = "@meta"


def utime() -> datetime:
    return datetime.now(timezone.utc).replace(tzinfo=None)
def utime_s() -> str:
    return datetime.now(timezone.utc).replace(tzinfo=None).isoformat()
@dataclass
class LunaObject:
    """Base class for all Luna objects with serialization capabilities"""
    u: uuid.uuid1
    type: BaseObjectType

    def __post_init__(self):
        """Initialize type after object creation"""
        self.u = uuid.uuid1()
        self.type = BaseObjectType.Base

    def toDict(self) -> dict:
        """Convert object to dictionary representation"""
        result = {}
        for key, value in self.__dict__.items():
            if hasattr(value, 'toDict'):
                result[key] = value.toDict()
            elif isinstance(value, Enum):
                result[key] = value.value
            else:
                result[key] = value
        return result

    @classmethod
    def fromDict(cls, data: dict) -> 'LunaObject':
        """Create object instance from dictionary data"""
        if not isinstance(data, dict):
            raise ValueError("Data must be a dictionary")
        
        # Handle type conversion for Enum fields
        processed_data = data.copy()
        if 'type' in processed_data and hasattr(cls, 'type'):
            processed_data['type'] = BaseObjectType(processed_data['type'])
        
        return cls(**processed_data)

    def __str__(self) -> str:
        """String representation of the object"""
        items = []
        for key, value in self.__dict__.items():
            if isinstance(value, Enum):
                items.append(f"{key}={value.value}")
            else:
                items.append(f"{key}={value}")
        return f"{self.__class__.__name__}({', '.join(items)})"

    def __hash__(self) -> int:
        """Generate hash based on object contents"""
        hash_values = []
        for key, value in sorted(self.__dict__.items()):
            if value is not None:
                if isinstance(value, (list, set)):
                    hash_values.append(tuple(value))
                elif isinstance(value, dict):
                    hash_values.append(tuple(sorted(value.items())))
                elif isinstance(value, Enum):
                    hash_values.append(value.value)
                else:
                    hash_values.append(value)
        return hash(tuple(hash_values))

@dataclass
class MetaObject(LunaObject):
    """Meta object type with additional handle field"""
    
    handle: Optional[Any] = field(default=None)
    
    def __post_init__(self):
        """Initialize type after object creation"""
        self.type = BaseObjectType.Meta