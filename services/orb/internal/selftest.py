
import yaml
import os
import atexit
import sys
import logging
import threading
import time

from .storage import *

class StorageTester:
    """
    Test class for DataStorage methods
    """
    
    def __init__(self, storage: DataStorage):
        """
        Initialize tester with DataStorage instance
        
        Args:
            storage: Initialized DataStorage instance for testing
        """
        self.storage = storage
        self.logger = logging.getLogger(__name__)
        self.test_table = "public.orb_meta"
        self.created_ids = []  # Track created records for cleanup
        
    def run_all_tests(self) -> bool:
        """
        Run all test methods and return overall success status
        
        Returns:
            bool: True if all tests passed, False otherwise
        """
        tests = [
            self.test_create_record,
            self.test_get_record,
            self.test_update_record,
            self.test_delete_record,
            self.test_find_records,
            self.test_get_table_info,
            self.test_execute_raw_query,
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
        
        # Cleanup test data
        self.cleanup_test_data()
        
        return failed == 0
    
    def test_create_record(self) -> bool:
        """Test creating a new record"""
        try:
            test_data = {
                'data_type': '@json',
                'ctime': datetime.utcnow(),
                'flags': ['test_flag'],
                'src': 12345
            }
            
            record_id = self.storage.create_record(self.test_table, test_data)
            
            if record_id and isinstance(record_id, int) and record_id > 0:
                self.created_ids.append(record_id)
                self.logger.debug(f"Created record with ID: {record_id}")
                return True
            return False
        except Exception as e:
            self.logger.error(f"test_create_record failed: {e}")
            return False
    
    def test_get_record(self) -> bool:
        """Test retrieving a record by ID"""
        try:
            # First create a test record
            test_data = {
                'data_type': '@json',
                'ctime': datetime.utcnow(),
                'flags': ['get_test'],
                'src': 67890
            }
            
            record_id = self.storage.create_record(self.test_table, test_data)
            self.created_ids.append(record_id)
            
            # Now try to retrieve it
            record = self.storage.get_record(self.test_table, record_id)
            
            if (record and 
                record['id'] == record_id and 
                record['data_type'] == '@json' and
                record['src'] == 67890):
                return True
            return False
        except Exception as e:
            self.logger.error(f"test_get_record failed: {e}")
            return False
    
    def test_update_record(self) -> bool:
        """Test updating an existing record"""
        try:
            # Create test record
            test_data = {
                'data_type': '@json',
                'ctime': datetime.utcnow(),
                'flags': ['before_update'],
                'src': 11111
            }
            
            record_id = self.storage.create_record(self.test_table, test_data)
            self.created_ids.append(record_id)
            
            # Update the record
            update_data = {
                'data_type': '@raw',
                'flags': ['after_update'],
                'src': 22222
            }
            
            success = self.storage.update_record(self.test_table, record_id, update_data)
            
            if not success:
                return False
            
            # Verify the update
            updated_record = self.storage.get_record(self.test_table, record_id)
            
            if (updated_record and 
                updated_record['data_type'] == '@raw' and
                updated_record['src'] == 22222):
                return True
            return False
        except Exception as e:
            self.logger.error(f"test_update_record failed: {e}")
            return False
    
    def test_delete_record(self) -> bool:
        """Test deleting a record"""
        try:
            # Create test record
            test_data = {
                'data_type': '@json',
                'ctime': datetime.utcnow(),
                'flags': ['to_delete'],
                'src': 33333
            }
            
            record_id = self.storage.create_record(self.test_table, test_data)
            
            # Delete the record
            success = self.storage.delete_record(self.test_table, record_id)
            
            if not success:
                return False
            
            # Verify deletion
            deleted_record = self.storage.get_record(self.test_table, record_id)
            
            return deleted_record is None
        except Exception as e:
            self.logger.error(f"test_delete_record failed: {e}")
            return False
    
    def test_find_records(self) -> bool:
        """Test finding records with conditions"""
        try:
            # Create test records with specific flags
            test_flags = ['find_test_1', 'find_test_2']
            
            for flag in test_flags:
                test_data = {
                    'data_type': '@json',
                    'ctime': datetime.utcnow(),
                    'flags': [flag],
                    'src': 44444
                }
                record_id = self.storage.create_record(self.test_table, test_data)
                self.created_ids.append(record_id)
            
            # Find records with specific condition
            conditions = {'src': 44444}
            records = self.storage.find_records(self.test_table, conditions)
            
            if (records and 
                len(records) >= 2 and
                all(record['src'] == 44444 for record in records)):
                return True
            return False
        except Exception as e:
            self.logger.error(f"test_find_records failed: {e}")
            return False
    
    def test_get_table_info(self) -> bool:
        """Test getting table structure information"""
        try:
            table_info = self.storage.get_table_info('orb_meta')
            
            if (table_info and 
                isinstance(table_info, list) and
                len(table_info) > 0):
                
                # Check if expected columns exist
                column_names = [col['column_name'] for col in table_info]
                expected_columns = ['id', 'data_type', 'ctime', 'flags', 'src']
                
                return all(col in column_names for col in expected_columns)
            return False
        except Exception as e:
            self.logger.error(f"test_get_table_info failed: {e}")
            return False
    
    def test_execute_raw_query(self) -> bool:
        """Test executing raw SQL queries"""
        try:
            # Create a test record
            test_data = {
                'data_type': '@json',
                'ctime': datetime.utcnow(),
                'flags': ['raw_query_test'],
                'src': 55555
            }
            
            record_id = self.storage.create_record(self.test_table, test_data)
            self.created_ids.append(record_id)
            
            # Execute raw query
            query = "SELECT COUNT(*) as count FROM public.orb_meta WHERE src = %s"
            result = self.storage.execute_raw_query(query, (55555,))
            
            if (result and 
                len(result) > 0 and 
                result[0]['count'] > 0):
                return True
            return False
        except Exception as e:
            self.logger.error(f"test_execute_raw_query failed: {e}")
            return False
    
    def test_push_meta_new(self) -> bool:
        """Test pushing new OrbMetaObject (without ID)"""
        try:
            # Create new OrbMetaObject without ID
            meta_obj = OrbMetaObject(
                id=None,  # This should trigger creation of new record
                subtype='@json',
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
                'flags': ['before_push_update'],
                'src': 77777
            }
            
            record_id = self.storage.create_record(self.test_table, test_data)
            self.created_ids.append(record_id)
            
            # Create OrbMetaObject with existing ID
            meta_obj = OrbMetaObject(
                id=record_id,
                subtype='@raw',  # Change subtype
                flags=['after_push_update'],
                handle=88888  # Change handle
            )
            
            # Push the object (should update)
            result_obj = self.storage.push_meta(meta_obj)
            
            if (result_obj and 
                result_obj.id == record_id):
                
                # Verify the update in database
                db_record = self.storage.get_record(self.test_table, record_id)
                
                if (db_record and 
                    db_record['data_type'] == '@raw' and
                    db_record['src'] == 88888):
                    return True
            return False
        except Exception as e:
            self.logger.error(f"test_push_meta_existing failed: {e}")
            return False
    
    def cleanup_test_data(self):
        """Clean up test records created during testing"""
        try:
            for record_id in self.created_ids:
                try:
                    self.storage.delete_record(self.test_table, record_id)
                except Exception as e:
                    self.logger.warning(f"Failed to cleanup record {record_id}: {e}")
            
            self.logger.info(f"Cleaned up {len(self.created_ids)} test records")
            self.created_ids.clear()
        except Exception as e:
            self.logger.error(f"Cleanup failed: {e}")
