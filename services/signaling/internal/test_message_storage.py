import time
from datetime import datetime
from .data_types import *

class MessageStorageTest:
    def __init__(self, storage):
        self.storage = storage

    def test_create_event(self):
        """Test function for creating an event with specified source"""
        # Create test data
        test_payload = {
            "action": "button_click",
            "element_id": "submit_button",
            "page_url": "https://example.com/form",
            "user_agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
        }
        
        test_affected = ["user_session_12345", "form_data_67890"]
        test_tags = ["ui_interaction", "form_submission", "analytics"]
        
        # Create EventData object
        event_data = EventData(
            event_type="user_interaction",
            payload=test_payload,
            timestamp=time.time(),  # Current time as Unix timestamp
            source="web_client",    # Specify source (owner will be set to "web_client")
            affected=test_affected,
            tags=test_tags
        )
        
        # Call the event creation function
        try:
            new_eid = self.storage.create_event(event_data)
            print(f"✅ Successfully created event with ID: {new_eid}")
            return new_eid
        except Exception as e:
            print(f"❌ Error creating event: {e}")
            return None

    def test_create_ownerless_event(self):
        """Test function for creating an event without specifying a source"""
        # Create test data
        test_payload = {
            "system_event": "auto_backup",
            "backup_size": "2.5GB",
            "duration_seconds": 120
        }
        
        # Create EventData object without source
        event_data = EventData(
            event_type="system_task",
            payload=test_payload,
            timestamp=time.time(),  # Current time
            # source not specified - should use "ownerless"
            affected=["database_primary", "storage_server"],
            tags=["system", "maintenance", "automated"]
        )
        
        # Call the event creation function
        try:
            new_eid = self.storage.create_event(event_data)
            print(f"✅ Successfully created ownerless event with ID: {new_eid}")
            return new_eid
        except Exception as e:
            print(f"❌ Error creating ownerless event: {e}")
            return None

    def test_create_minimal_event(self):
        """Test function for creating an event with minimal required fields"""
        # Create minimal test data
        test_payload = {
            "minimal": "data"
        }
        
        # Create EventData object with only required fields
        event_data = EventData(
            event_type="minimal_event",
            payload=test_payload,
            timestamp=time.time(),  # Current time
            # source, affected, and tags not specified
        )
        
        # Call the event creation function
        try:
            new_eid = self.storage.create_event(event_data)
            print(f"✅ Successfully created minimal event with ID: {new_eid}")
            return new_eid
        except Exception as e:
            print(f"❌ Error creating minimal event: {e}")
            return None