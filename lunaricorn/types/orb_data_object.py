from dataclasses import dataclass, field
from enum import Enum
from .object import *
from lunaricorn.utils.db_manager import *
from datetime import datetime, timezone
import json
import uuid

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

    @classmethod
    def from_record(cls, record: dict) -> 'OrbDataObject':
        """
        Create an OrbDataObject instance from a database record.
        
        Args:
            record: Dictionary with database record fields:
                - u (str): UUID as string
                - data_type (str): Data type (maps to subtype)
                - chain_left (str or None): Left chain UUID as string
                - chain_right (str or None): Right chain UUID as string
                - parent (str or None): Parent UUID as string
                - ctime (datetime or str): Creation time
                - flags (list or str): List of flags or JSON string
                - src (str or None): Source text
                - data (dict or str): Data as dictionary or JSON string
                
        Returns:
            OrbDataObject: Initialized instance
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
        
        # Parse chain_left, chain_right, parent (UUIDs)
        chain_left = uuid.UUID(record['chain_left']) if record.get('chain_left') else None
        chain_right = uuid.UUID(record['chain_right']) if record.get('chain_right') else None
        parent = uuid.UUID(record['parent']) if record.get('parent') else None
        
        # Parse data
        data = record.get('data')
        if isinstance(data, str):
            try:
                data = json.loads(data)
            except json.JSONDecodeError:
                # If it's not valid JSON, keep as string (for @raw subtype)
                pass
        
        # Parse subtype (from data_type field)
        subtype = record.get('data_type', '@json')
        
        # Parse src
        src = record.get('src')
        
        # Create and return the object
        return cls(
            u=u,
            src=src,
            data=data,
            chain_left=chain_left,
            chain_right=chain_right,
            parent=parent,
            ctime=ctime,
            flags=flags,
            subtype=subtype
        )
