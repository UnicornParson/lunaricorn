from typing import Dict, Any, Optional, List
from dataclasses import dataclass, asdict
from enum import Enum

@dataclass
class EventData:
    """Event data structure"""
    event_type: str
    payload: Dict[str, Any]
    timestamp: float
    source: Optional[str] = None
    affected: Optional[list]= None
    tags: Optional[list]= None

@dataclass
class EventDataExtended(EventData):
    eid: int = 0
    @classmethod
    def from_event_data(cls, event_data: EventData, eid: int = 0) -> 'EventDataExtended':
        data_dict = asdict(event_data)
        return cls(**data_dict, eid=eid)



@dataclass
class Subscriber:
    """Subscriber information"""
    subscriber_id: str
    event_types: List[str]  # Event types this subscriber is interested in
    last_seen: float
    metadata: Dict[str, Any]