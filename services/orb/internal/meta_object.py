import os
from dataclasses import dataclass, field
from typing import Union
from enum import Enum
from lunaricorn.types import *
from lunaricorn.utils.db_manager import *
from datetime import datetime, timezone
import json



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
        
    @classmethod
    def from_record(cls, record: dict) -> 'OrbMetaObject':
        """
        Create an OrbMetaObject instance from a database record.
        
        Args:
            record: Dictionary with database record fields:
                - id (int): Record ID
                - u (str): UUID as string
                - data_type (str): Data type
                - ctime (datetime or str): Creation time
                - flags (list or str): List of flags or JSON string
                - src (int): Source/handle
                
        Returns:
            OrbMetaObject: Initialized instance
        """
        # Parse UUID
        u = uuid.UUID(record['u']) if isinstance(record['u'], str) else record['u']
        
        # Parse creation time
        if isinstance(record['ctime'], str):
            ctime = datetime.fromisoformat(record['ctime'].replace('Z', '+00:00'))
        else:
            ctime = record['ctime']
        
        # Parse flags
        if isinstance(record['flags'], str):
            flags = json.loads(record['flags'])
        else:
            flags = record['flags']
        
        # Create and return the object
        return cls(
            id=record['id'],
            u=u,
            type="@OrbMeta",
            handle=record['src'],  # Map 'src' field to 'handle' attribute
            flags=flags,
            ctime=ctime
        )
    
    def __str__(self) -> str:
        """Return a human-readable string representation of the OrbMetaObject."""
        ctime_str = self.ctime.isoformat() if self.ctime else "None"
        flags_str = json.dumps(self.flags) if self.flags else "[]"
        handle_str = str(self.handle) if self.handle is not None else "None"
        
        return (f"OrbMetaObject("
                f"id={self.id}, "
                f"u={self.u}, "
                f"handle={handle_str}, "
                f"type={self.type}, "
                f"ctime={ctime_str}, "
                f"flags={flags_str})")
