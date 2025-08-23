from typing import Dict, Any, Optional, List
from dataclasses import dataclass
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
class Subscriber:
    """Subscriber information"""
    subscriber_id: str
    event_types: List[str]  # Event types this subscriber is interested in
    last_seen: float
    metadata: Dict[str, Any]