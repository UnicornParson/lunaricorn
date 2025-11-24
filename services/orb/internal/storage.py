# services/orb/internal/storage.py
from lunaricorn.utils.db_manager import *
from datetime import datetime
import json
import time as time_module
import logging
from typing import List, Dict, Any, Optional
from .orb_database_manager import *
from .orb_types import *
class StorageError(Exception):
    pass

class BrokenStorageError(Exception):
    def __init__(self, message):
        self.message = message
        super().__init__(self.message)
    
    def __str__(self):
        return f"BrokenStorageError: {self.message}"

class DataStorage:
    def __init__(self, db_cfg: DbConfig):
        self.db_cfg = db_cfg
        self.ready = False
        self.logger = logging.getLogger(__name__)
        if not db_cfg.valid():
            raise ValueError("invalid db config")
        try:
            # Initialize database connection
            db_manager = OrbDatabaseManager()
            db_manager.initialize(
                host=db_cfg.db_host,
                port=db_cfg.db_port,
                user=db_cfg.db_user,
                password=db_cfg.db_password,
                dbname=db_cfg.db_dbname,
                minconn=1,
                maxconn=3
            )
            db_manager.install_db()
            self.db_manager = db_manager
            self.db_enabled = True
            self.logger.info("Database connection initialized for orb service")

        except Exception as e:
            self.logger.error(f"Failed to initialize database connection: {e}")
            self.db_enabled = False
            raise BrokenStorageError(f"cannot init storage. reason: {e}")
        
    def good(self) -> bool:
        return self.db_enabled
    
    def create_record(self, table_name: str, data: Dict[str, Any]) -> int:
        """Create a new record in the specified table"""
        # Build INSERT query dynamically
        columns = list(data.keys())
        values = list(data.values())
        
        placeholders = ",".join(["%s"] * len(columns))
        columns_str = ",".join(columns)
        
        query = f"""
            INSERT INTO {table_name} ({columns_str})
            VALUES ({placeholders})
            RETURNING id;
        """
        
        result = self.db_manager.execute_query(
            query=query,
            params=values,
            fetch_one=True
        )
        
        return result[0] if result else None

    def get_record(self, table_name: str, record_id: int) -> Optional[Dict[str, Any]]:
        """Get a record by ID from the specified table"""
        query = f"""
            SELECT * FROM {table_name}
            WHERE id = %s
        """
        
        result = self.db_manager.execute_query(
            query=query,
            params=(record_id,),
            fetch_one=True
        )
        
        if result:
            # Convert result to dictionary
            columns = [desc[0] for desc in self.db_manager.cursor.description]
            return dict(zip(columns, result))
        return None

    def update_record(self, table_name: str, record_id: int, data: Dict[str, Any]) -> bool:
        """Update a record in the specified table"""
        # Build UPDATE query dynamically
        set_clause = ",".join([f"{key} = %s" for key in data.keys()])
        values = list(data.values()) + [record_id]
        
        query = f"""
            UPDATE {table_name}
            SET {set_clause}
            WHERE id = %s
        """
        
        try:
            self.db_manager.execute_query(
                query=query,
                params=values,
                fetch_one=False
            )
            return True
        except Exception as e:
            self.logger.error(f"Failed to update record: {e}")
            return False

    def delete_record(self, table_name: str, record_id: int) -> bool:
        """Delete a record from the specified table"""
        query = f"""
            DELETE FROM {table_name}
            WHERE id = %s
        """
        
        try:
            self.db_manager.execute_query(
                query=query,
                params=(record_id,),
                fetch_one=False
            )
            return True
        except Exception as e:
            self.logger.error(f"Failed to delete record: {e}")
            return False

    def find_records(self, table_name: str, conditions: Dict[str, Any] = None, 
                     limit: int = 0, offset: int = 0) -> List[Dict[str, Any]]:
        """Find records in the specified table with optional conditions"""
        query = f"SELECT * FROM {table_name}"
        params = []
        
        if conditions:
            where_conditions = []
            for key, value in conditions.items():
                where_conditions.append(f"{key} = %s")
                params.append(value)
            
            query += " WHERE " + " AND ".join(where_conditions)
        
        query += " ORDER BY id DESC"
        
        if limit > 0:
            query += f" LIMIT {limit}"
            if offset > 0:
                query += f" OFFSET {offset}"
        
        result = self.db_manager.execute_query(
            query=query,
            params=params,
            fetch_one=False,
            fetch_all=True
        )
        
        # Convert results to list of dictionaries
        if result and self.db_manager.cursor.description:
            columns = [desc[0] for desc in self.db_manager.cursor.description]
            return [dict(zip(columns, row)) for row in result]
        
        return []

    def get_table_info(self, table_name: str) -> Dict[str, Any]:
        """Get information about a table structure"""
        query = """
            SELECT column_name, data_type, is_nullable, column_default
            FROM information_schema.columns
            WHERE table_name = %s
            ORDER BY ordinal_position
        """
        
        result = self.db_manager.execute_query(
            query=query,
            params=(table_name,),
            fetch_one=False,
            fetch_all=True
        )
        
        if result:
            columns = [desc[0] for desc in self.db_manager.cursor.description]
            return [dict(zip(columns, row)) for row in result]
        
        return []

    def execute_raw_query(self, query: str, params: tuple = None) -> List[Dict[str, Any]]:
        """Execute a raw SQL query and return results"""
        result = self.db_manager.execute_query(
            query=query,
            params=params,
            fetch_one=False,
            fetch_all=True
        )
        
        if result and self.db_manager.cursor.description:
            columns = [desc[0] for desc in self.db_manager.cursor.description]
            return [dict(zip(columns, row)) for row in result]
        
        return []

    def push_meta(self, meta_obj: OrbMetaObject) -> OrbMetaObject:
        """
        Push OrbMetaObject to the database. 
        If record doesn't exist, creates new one. If exists, updates it.
        
        Args:
            meta_obj: OrbMetaObject instance to push to database
            
        Returns:
            OrbMetaObject: Updated object with database ID if new record was created
            
        Raises:
            StorageError: If database operation fails
            BrokenStorageError: If database connection is not available
        """
        if not self.db_enabled:
            self.logger.error("Database is not enabled, cannot push meta object")
            raise BrokenStorageError("Database connection is not available")
        
        try:
            # Prepare data for database operation
            data = {
                'data_type': meta_obj.subtype if hasattr(meta_obj, 'subtype') else '@json',
                'flags': meta_obj.flags if hasattr(meta_obj, 'flags') else [],
                'src': meta_obj.handle
            }
            
            # Check if this is a new record (id is None, <= 0, or doesn't exist)
            is_new_record = (not hasattr(meta_obj, 'id') or 
                           meta_obj.id is None or 
                           meta_obj.id <= 0)
            
            if is_new_record:
                # Insert new record
                self.logger.info("Creating new orb_meta record")
                data['ctime'] = datetime.utcnow()
                
                new_id = self.create_record('public.orb_meta', data)
                if new_id is None:
                    raise StorageError("Failed to create new orb_meta record")
                
                # Update the object with the new ID
                meta_obj.id = new_id
                self.logger.debug(f"Created new orb_meta record with ID: {new_id}")
                
            else:
                # Update existing record
                self.logger.info(f"Updating existing orb_meta record with ID: {meta_obj.id}")
                
                # For updates, we don't change the ctime
                success = self.update_record('public.orb_meta', meta_obj.id, data)
                if not success:
                    raise StorageError(f"Failed to update orb_meta record with ID: {meta_obj.id}")
                
                self.logger.debug(f"Successfully updated orb_meta record with ID: {meta_obj.id}")
            
            return meta_obj
            
        except StorageError:
            # Re-raise StorageError
            raise
        except Exception as e:
            self.logger.error(f"Unexpected error during push_meta operation: {e}")
            raise StorageError(f"Failed to push meta object: {e}")
