
import yaml
import os
import atexit
import sys
import logging
from .utils  import *

from .storage import *

class StorageTester:
    """
    Test class for DataStorage methods
    """
    
    def __init__(self, storage: DataStorage):
        self.storage = storage
        self.logger = logging.getLogger(__name__)
        self.test_table = "public.orb_meta"
        self.created_ids = []  # Track created records for cleanup
        
    def run_all_tests(self) -> bool:
        tests = [
            self.test_push_meta_new,
            self.test_push_meta_existing
        ]
        
        self.logger.info("Starting DataStorage test suite...")
        
        passed = 0
        failed = 0
        
        for test_method in tests:
            test_name = test_method.__name__
            try:
                result = test_method()
                if result:
                    self.logger.info(f"✓ {test_name} - PASSED")
                    passed += 1
                else:
                    self.logger.error(f"✗ {test_name} - FAILED")
                    failed += 1
            except Exception as e:
                self.logger.error(f"✗ {test_name} - ERROR: {e}")
                failed += 1
        
        self.logger.info(f"Test results: {passed} passed, {failed} failed")
        
        return failed == 0


    def test_push_meta_new(self) -> bool:
        """Test pushing new OrbMetaObject (without ID)"""
        try:
            # Create new OrbMetaObject without ID - use correct attribute names
            meta_obj = OrbMetaObject(
                id=None,  # This should trigger creation of new record
                flags=['test_push_new'],
                handle=66666
            )
            
            # Push the object
            result_obj = self.storage.push_meta(meta_obj)
            
            if (result_obj and 
                hasattr(result_obj, 'id') and 
                result_obj.id is not None and 
                result_obj.id > 0):
                
                self.created_ids.append(result_obj.id)
                return True
            return False
        except Exception as e:
            self.logger.error(f"test_push_meta_new failed: {e}")
            return False
    
    def test_push_meta_existing(self) -> bool:
        """Test pushing existing OrbMetaObject (with ID)"""
        try:
            # First create a record
            test_data = {
                'data_type': '@json',
                'ctime': datetime.utcnow(),
                'flags': json.dumps(['before_push_update']),  # Convert to JSON string
                'src': 77777
            }
            self.logger.info(f"p {line_numb()}")

            record_id = self.storage.create_record(self.test_table, test_data)

            self.logger.info(f"p {line_numb()}")
            self.created_ids.append(record_id)
            
            # Create OrbMetaObject with existing ID - use correct attribute names
            meta_obj = OrbMetaObject(
                id=record_id,
                flags=['after_push_update'],
                handle=88888  # Change handle
            )
            self.logger.info(f"p {line_numb()}")
            # Push the object (should update)
            result_obj = self.storage.push_meta(meta_obj)
            self.logger.info(f"p {line_numb()}")
            if (result_obj and  int(result_obj.id) == int(record_id)):
                self.logger.info(f"p {line_numb()} id ok")
                # Verify the update in database

                db_record = self.storage.get_record(self.test_table, record_id, columns=["id", "data_type", "ctime", "flags", "src"])
                self.logger.info(f"p {line_numb()} get_record {db_record}")
                if (db_record and 
                    db_record['data_type'] == '@json' and
                    db_record['src'] == 88888):
                    self.logger.info(f"p {line_numb()}")
                    return True
            self.logger.info(f"p {line_numb()}:  res: {result_obj}, record_id:{record_id}")
            return False
        except Exception as e:
            self.logger.error(f"test_push_meta_existing failed: {e}")
            self.logger.info(f"p {line_numb()}")
            return False

