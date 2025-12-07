
import yaml
import os
import atexit
import sys
import logging
from .utils  import *
from datetime import datetime, timezone
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
            self.test_push_meta_existing,
            self.test_push_data_new,
            self.test_push_data_existing
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

    def test_push_data_new(self) -> bool:
        self.logger.info("🡒 Test pushing new OrbDataObject (without ID)")
        try:
            # Create new OrbDataObject without ID - use correct attribute names
            data_obj = OrbDataObject(
                u=None,
                type = "@OrbData",
                chain_left = None,
                chain_right = None,
                parent = None,
                subtype='@json',
                src=None,
                data={'key': 'value'},
                flags=['test_push_new']
            )
            
            # Push the object
            result_obj = self.storage.push_data(data_obj)
            self.logger.info(f"after push result_obj: {result_obj}")
            if (result_obj and 
                hasattr(result_obj, 'u') and 
                result_obj.u is not None):
                
                self.created_ids.append(result_obj.u)
                return True
            return False
        except Exception as e:
            self.logger.error(f"test_push_data_new failed: {e}")
            traceback.print_exc()
            return False
    
    def test_push_data_existing(self) -> bool:
        self.logger.info("🡒 Test pushing existing OrbDataObject (with ID)")
        data_table = "public.orb_data"
        try:
            # First create a record
            u = uuid.uuid7()
            test_data = {
                'u': u,
                'ctime': utime_s(),
                'chain_left' : None,
                'chain_right' : None,
                'parent' : None,
                'data_type': '@json',
                'src': None, 
                'data': {'key': 'value'},
                'flags': ['before_push_update']
            }
            self.logger.info(f"@@ test_push_data_existing p {line_numb()}")
            # Assuming there's a method to create a record with auto-generated ID
            record_id = self.storage.create_record(data_table, test_data, id_field="u")

            self.logger.info(f"@@ test_push_data_existing p {line_numb()}")
            # Create OrbDataObject with existing ID - use correct attribute names
            data_obj = OrbDataObject(
                u=u,
                chain_left = None,
                chain_right = None,
                parent = None,
                type = "@OrbData",
                subtype='@json',
                src=None, 
                data={'key': 'updated_value'},
                flags=['after_push_update']
            )
            self.logger.info(f"@@ test_push_data_existing p {line_numb()}")
            # Push the object (should update)
            result_obj = self.storage.push_data(data_obj)
            self.logger.info(f"@@ test_push_data_existing p {line_numb()}")
            if (result_obj and  str(result_obj.u) == str(record_id)):
                # Verify the update in database
                db_record = self.storage.get_record(data_table, record_id, columns=["u", "subtype", "src", "left", "right", "data", "flags"], id_field='u')
                self.logger.info(f"@@ test_push_data_existing db_record {db_record}")
                if (db_record and 
                    db_record['subtype'] == '@json' and
                    db_record['src'] is None):
                    return True
            return False
        except Exception as e:
            self.logger.error(f"test_push_data_existing failed: {e}")
            traceback.print_exc()
            return False


    def test_push_meta_new(self) -> bool:
        self.logger.info("🡒 Test pushing new OrbMetaObject (without ID)")
        try:
            # Create new OrbMetaObject without ID - use correct attribute names
            meta_obj = OrbMetaObject(
                id=None,  # This should trigger creation of new record
                u=uuid.uuid7(),
                type = "@OrbMeta",
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
            traceback.print_exc()
            return False
    
    def test_push_meta_existing(self) -> bool:
        self.logger.info("🡒 Test pushing existing OrbMetaObject (with ID)")
        try:
            # First create a record
            u = uuid.uuid7()
            test_data = {
                'data_type': '@json',
                'ctime': utime_s(),
                'flags': json.dumps(['before_push_update']),  # Convert to JSON string
                'src': 77777,
                'u':u
            }
            
            self.logger.info(f"p {line_numb()}")

            record_id = self.storage.create_record(self.test_table, test_data, id_field="id")

            self.logger.info(f"p {line_numb()}")
            self.created_ids.append(record_id)
            
            # Create OrbMetaObject with existing ID - use correct attribute names
            meta_obj = OrbMetaObject(
                u=u,
                type = "@OrbMeta",
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

                db_record = self.storage.get_record(self.test_table, record_id, columns=["id", "u", "data_type", "ctime", "flags", "src"])
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
            traceback.print_exc()
            return False

