from typing import Dict, Any, Optional, List
from dataclasses import dataclass, asdict
from enum import Enum
import re
from datetime import datetime

@dataclass
class EventData:
    """Event data structure"""
    event_type: str
    payload: Dict[str, Any]
    timestamp: float
    source: Optional[str] = None
    affected: Optional[list]= None
    tags: Optional[list]= None

    @classmethod
    def from_dict(cls, data: dict) -> 'EventData':
        timestamp = data['ctime'].timestamp() if isinstance(data['ctime'], datetime) else data['ctime']
        
        return cls(
            event_type=data['type'],
            payload=data['payload'],
            timestamp=timestamp,
            source=data.get('owner'),
            affected=data.get('affected'),
            tags=data.get('tags')
        )

@dataclass
class EventDataExtended(EventData):
    eid: int = 0
    @classmethod
    def from_event_data(cls, event_data: EventData, eid: int = 0) -> 'EventDataExtended':
        data_dict = asdict(event_data)
        return cls(**data_dict, eid=eid)
    @classmethod
    def from_dict(cls, data: dict) -> 'EventDataExtended':
        # Создаем базовый EventData из словаря
        event_data = EventData.from_dict(data)
        
        # Преобразуем в dict и добавляем eid
        data_dict = asdict(event_data)
        data_dict['eid'] = data['eid']
        
        return cls(**data_dict)

@dataclass
class BrowseRequest():
    event_types: List[str]
    timestamp: float
    sources: List[str]
    affected: List[str]
    tags: List[str]
    limit: int = 0
    def __init__(
        self,
        event_types: List[str],
        timestamp: float,
        sources: List[str],
        affected: List[str],
        tags: List[str],
        limit: int = 0
    ) -> None:
        self.event_types = event_types
        self.timestamp = timestamp
        self.sources = sources
        self.affected = affected
        self.tags = tags
        self.limit = limit

    @classmethod
    def from_dict(cls, data: Dict[str, Any]):
        event_types = data.get("event_types", [])
        timestamp = data.get("timestamp", 0.0)
        sources = data.get("sources", [])
        affected = data.get("affected", [])
        tags = data.get("tags", [])
        limit = data.get("limit", 0)

        # Возвращаем экземпляр класса
        return cls(
            event_types=event_types,
            timestamp=timestamp,
            sources=sources,
            affected=affected,
            tags=tags,
            limit=limit
        )
    def __str__(self) -> str:
        """
        String serializer for BrowseRequest.
        Returns a human-readable string representation of the request.
        """
        return (
            f"BrowseRequest("
            f"event_types={self.event_types}, "
            f"timestamp={self.timestamp}, "
            f"sources={self.sources}, "
            f"affected={self.affected}, "
            f"tags={self.tags}, "
            f"limit={self.limit})"
        )
    def is_valid(self) -> bool:
        pattern = re.compile(
            r"(\b(ALTER|CREATE|DELETE|DROP|EXEC(UTE)?|INSERT( +INTO)?|"
            r"MERGE|SELECT|UPDATE|UNION( +ALL)?|OR|AND)\b|"
            r"[';\\-]|/\*|\*/|--|@@|@)",
            re.IGNORECASE
        )
        string_fields = [
            self.event_types,
            self.sources,
            self.affected,
            self.tags
        ]
        
        for field in string_fields:
            for value in field:
                if pattern.search(value):
                    return False
        return True
@dataclass
class Subscriber:
    """Subscriber information"""
    subscriber_id: str
    event_types: List[str]  # Event types this subscriber is interested in
    last_seen: float
    metadata: Dict[str, Any]