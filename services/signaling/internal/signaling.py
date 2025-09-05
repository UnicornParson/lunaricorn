import time
import logging
import os
import json
from typing import Dict, Any, Optional, List
from dataclasses import dataclass
from enum import Enum
from .data_types import *
from .message_storage import *
from .test_message_storage  import *

# Add lunaricorn to path for imports
## sys.path.append(os.path.join(os.path.dirname(__file__), '../../../..'))
from lunaricorn.utils.db_manager import *

class SignalingMessageType(Enum):
    """Message types for signaling server"""
    PUSH_EVENT = "pushevent"
    SUBSCRIBE = "subscribe"

# TODO: убрать локальное хранилище событий
# TODO: убрать механизм подписчиков

class Signaling:
    """Signaling service for handling event push and subscription"""
    
    def __init__(self, config: Dict[str, Any], publish_callback):
        self.config = config
        self.logger = logging.getLogger(__name__)
        self.publish_callback = publish_callback
        
        # Subscriber registry
        self.subscribers: Dict[str, Subscriber] = {}
        
        # Event storage (in-memory for now, could be moved to DB)
        self.events: List[EventData] = []
        
        self.storage = None
        self._setup_storage()
        
        # Configuration
        self.subscriber_timeout = config.get("message_storage", {}).get("subscriber_timeout", 300)  # 5 minutes
        self.max_events = config.get("message_storage", {}).get("max_events", 1000)  # Max events to keep in memory
    

    def _setup_storage(self):
        """Setup database connection with environment variable override"""
        # Read config file values first
        dcfg = DbConfig()
        dcfg.db_type = self.config.get("message_storage", {}).get("db_type", "postgresql")
        dcfg.db_host = self.config.get("message_storage", {}).get("db_host", "localhost")
        dcfg.db_port = self.config.get("message_storage", {}).get("db_port", 5432)
        dcfg.db_user = self.config.get("message_storage", {}).get("db_user", "postgres")
        dcfg.db_password = self.config.get("message_storage", {}).get("db_password", "postgres")
        dcfg.db_dbname = self.config.get("message_storage", {}).get("dbname", "lunaricorn")
        self.logger.info(f"apply env {dcfg.db_user}@{dcfg.db_host}:{dcfg.db_port}/{dcfg.db_dbname}")
        # Environment variables override config file values only if they are set
        if "db_type" in os.environ:
            dcfg.db_type = os.environ["db_type"]
        if "db_host" in os.environ:
            dcfg.db_host = os.environ["db_host"]
        if "db_port" in os.environ:
            dcfg.db_port = int(os.environ["db_port"])
        if "db_user" in os.environ:
            dcfg.db_user = os.environ["db_user"]
        if "db_password" in os.environ:
            dcfg.db_password = os.environ["db_password"]
        if "db_name" in os.environ:
            dcfg.db_dbname = os.environ["db_name"]
        self.logger.info(f"try ti connect to db {dcfg.to_str()}")
        self.storage = MessageStorage(dcfg)

        # test
        mst = MessageStorageTest(self.storage)
        self.logger.info(f"test_create_event rc: {mst.test_create_event()}")
        self.logger.info(f"test_create_event rc: {mst.test_create_ownerless_event()}")

    def notify_subscribers(self, event:EventDataExtended):
        if self.publish_callback:
            self.publish_callback(event)
        else:
            self.logger.error(f"no publish callback")
    
    def push(self, data: EventData):
        print("try push")
        eid = 0
        try:
            eid = self.storage.create_event(data)
            ex = EventDataExtended.from_event_data(data, eid=eid)
            self.events.append(ex)
            self.notify_subscribers(ex)
        except Exception as e:
            self.logger.error(f"Failed to store event in database: {e}")
            return -1
        
        return eid

    # TODO:  for remove
    def handle_push_event(self, data: Dict[str, Any]) -> Dict[str, Any]:
        """Handle pushevent message to store and distribute events"""
        event_type = data.get("event_type")
        payload = data.get("payload", {})
        source = data.get("source")
        tags = data.get("source", [])
        affected = data.get("source", [])


        if not event_type:
            return {"status": "error", "message": "Missing required field: event_type"}
        
        # Create event data
        event = EventData(
            event_type = event_type,
            payload = payload,
            timestamp = time.time(),
            source = source,
            tags = tags,
            affected = affected
        )
        
        # Store event
        self.events.append(event)
        eid = 0
        try:
            eid = self.storage.create_event(event)
        except Exception as e:
            self.logger.error(f"Failed to store event in database: {e}")
            return {"status": "failed"}
        
        self.notify_subscribers(event, eid)
        
        return {
            "status": "success",
            "event_id": eid
        }
    
    def handle_subscribe(self, data: Dict[str, Any]) -> Dict[str, Any]:
        """Handle subscribe message to register subscriber for event types"""
        subscriber_id = data.get("subscriber_id")
        event_types = data.get("event_types", [])
        metadata = data.get("metadata", {})
        
        if not subscriber_id:
            return {"status": "error", "message": "Missing subscriber_id"}
        
        if not event_types:
            return {"status": "error", "message": "Missing event_types"}
        
        # Create or update subscriber
        subscriber = Subscriber(
            subscriber_id=subscriber_id,
            event_types=event_types,
            last_seen=time.time(),
            metadata=metadata
        )
        
        self.subscribers[subscriber_id] = subscriber

        try:
            self._store_subscriber_in_db(subscriber)
        except Exception as e:
            self.logger.error(f"Failed to store subscriber in database: {e}")
            return {"status": "failed"}
        
        # Get recent events of interest
        matching_events = []
        for event in self.events[-10:]:  # Return last 10 matching events
            if event.event_type in event_types or "*" in event_types:
                matching_events.append({
                    "event_type": event.event_type,
                    "payload": event.payload,
                    "timestamp": event.timestamp,
                    "source": event.source
                })
        
        self.logger.info(f"Subscriber {subscriber_id} subscribed to {event_types}, returning {len(matching_events)} recent events")
        
        return {
            "status": "success", 
            "message": "Subscription successful",
            "subscriber_id": subscriber_id,
            "event_types": event_types,
            "recent_events": matching_events
        }


    def _store_subscriber_in_db(self, subscriber: Subscriber):
        """Store subscriber in database"""
        if not self.db_enabled:
            return
        
        try:
            query = """
                INSERT INTO subscribers (subscriber_id, event_types, last_seen, metadata)
                VALUES (%s, %s, %s, %s)
                ON CONFLICT (subscriber_id) DO UPDATE SET
                    event_types = EXCLUDED.event_types,
                    last_seen = EXCLUDED.last_seen,
                    metadata = EXCLUDED.metadata
            """
            
            
            event_types_json = json.dumps(subscriber.event_types)
            metadata_json = json.dumps(subscriber.metadata)
            
            self.db_manager.execute_query(query, (
                subscriber.subscriber_id,
                event_types_json,
                int(subscriber.last_seen),
                metadata_json
            ))
            self.logger.debug(f"Stored subscriber {subscriber.subscriber_id} in database")
            
        except Exception as e:
            self.logger.error(f"Failed to store subscriber in database: {e}")
            raise
    
    def cleanup_dead_subscribers(self):
        """Remove dead subscribers from in-memory registry"""
        current_time = time.time()
        cutoff_time = current_time - self.subscriber_timeout
        
        dead_subscribers = []
        for subscriber_id, subscriber in self.subscribers.items():
            if subscriber.last_seen <= cutoff_time:
                dead_subscribers.append(subscriber_id)
        
        for subscriber_id in dead_subscribers:
            subscriber = self.subscribers[subscriber_id]
            del self.subscribers[subscriber_id]
            self.logger.info(f"Removed dead subscriber {subscriber_id}")
    
    def get_events_for_subscriber(self, subscriber_id: str, event_types: List[str], limit: int = 10) -> List[Dict[str, Any]]:
        """Get recent events for a specific subscriber"""
        if subscriber_id not in self.subscribers:
            return []
        
        matching_events = []
        for event in self.events[-limit:]:
            if event.event_type in event_types or "*" in event_types:
                matching_events.append({
                    "event_type": event.event_type,
                    "payload": event.payload,
                    "timestamp": event.timestamp,
                    "source": event.source
                })
        
        return matching_events
    
    def shutdown(self):
        """Shutdown signaling service"""
        if self.db_enabled and hasattr(self, 'db_manager'):
            try:
                self.db_manager.shutdown()
                self.logger.info("Database connection closed")
            except Exception as e:
                self.logger.error(f"Error shutting down database: {e}") 