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

    def notify_subscribers(self, event:EventDataExtended):
        if self.publish_callback:
            self.publish_callback(event)
        else:
            self.logger.error(f"no publish callback")
    
    def push(self, data: EventData):
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

    def shutdown(self):
        """Shutdown signaling service"""
        if self.db_enabled and hasattr(self, 'db_manager'):
            try:
                self.db_manager.shutdown()
                self.logger.info("Database connection closed")
            except Exception as e:
                self.logger.error(f"Error shutting down database: {e}") 