import psycopg2
import psycopg2.extras
from psycopg2.pool import SimpleConnectionPool
import time
from typing import List, Dict, Optional
import logging

logger = logging.getLogger(__name__)

from .db_manager import db_manager

class DiscoverManagerPG:
    """
    Manager for discovering and updating node information in a PostgreSQL database.
    Uses the global database manager for connection management.
    """
    def __init__(self, host: str, port: int, user: str, password: str, dbname: str, minconn: int = 1, maxconn: int = 5):
        """
        Initialize the manager.
        Uses the global database manager for connection management.
        """
        # Store connection parameters for reference
        self.host = host
        self.port = port
        self.user = user
        self.password = password
        self.dbname = dbname
        self.minconn = minconn
        self.maxconn = maxconn
        
        logger.info("DiscoverManagerPG initialized with global database manager")

    def _create_pool(self):
        """Create a new connection pool."""
        try:
            if self.pool:
                self.pool.closeall()
            self.pool = SimpleConnectionPool(self.minconn, self.maxconn, **self.conn_params)
            logger.info("Connection pool created successfully")
        except Exception as e:
            logger.error(f"Failed to create connection pool: {e}")
            raise

    def _get_conn(self):
        """Get a connection from the global database manager."""
        return db_manager.get_connection()

    def _put_conn(self, conn):
        """Return a connection to the global database manager."""
        db_manager.return_connection(conn)

    def _reset_pool(self):
        """Reset the connection pool when it gets exhausted."""
        try:
            logger.warning("Resetting connection pool due to exhaustion")
            self._create_pool()
            logger.info("Connection pool reset successfully")
        except Exception as e:
            logger.error(f"Failed to reset connection pool: {e}")
            raise

    def _get_connection_context(self):
        """Context manager for database connections."""
        conn = None
        try:
            conn = self._get_conn()
            return conn
        except Exception as e:
            if conn:
                self._put_conn(conn)
            raise
        return conn

    def get_pool_status(self):
        """Get current connection pool status."""
        try:
            return {
                'minconn': self.minconn,
                'maxconn': self.maxconn,
                'pool_size': self.pool.get_size(),
                'pool_used': self.pool.get_used()
            }
        except Exception as e:
            logger.error(f"Error getting pool status: {e}")
            return {}

    def _is_pool_valid(self):
        """Check if the global database manager is valid."""
        return db_manager.validate_connection()

    def _validate_connection(self):
        """Validate that a connection can be acquired from the global database manager."""
        return db_manager.validate_connection()

    def _ensure_table_exists(self):
        """Ensure all required tables exist using the global database manager."""
        db_manager.ensure_tables_exist()

    def update(self, node_name: str, node_type: str, instance_key: str, host: Optional[str] = None, port: Optional[int] = 0) -> bool:
        """
        Update node information in the database.
        If record exists with same instance_key, update fields and timestamp.
        If not exists, create new record.
        """
        if not self._validate_connection():
            logger.error("Cannot update node - no valid database connection")
            return False
        
        current_timestamp = int(time.time())
        
        try:
            # Check if record exists
            existing_record = db_manager.execute_query(
                "SELECT i FROM last_seen WHERE key = %s",
                (instance_key,),
                fetch_one=True
            )
            
            if existing_record:
                # Update existing record
                db_manager.execute_query('''
                    UPDATE last_seen 
                    SET name = %s, type = %s, last_update = %s, key = %s
                    WHERE key = %s
                ''', (node_name, node_type, current_timestamp, instance_key, instance_key))
                logger.debug(f"Updated existing node: {node_name} ({instance_key})")
            else:
                # Insert new record
                db_manager.execute_query('''
                    INSERT INTO last_seen (name, type, key, last_update)
                    VALUES (%s, %s, %s, %s)
                ''', (node_name, node_type, instance_key, current_timestamp))
                logger.debug(f"Added new node: {node_name} ({instance_key})")
            
            return True
        except Exception as e:
            logger.error(f"Error updating node {node_name}: {e}")
            return False

    def list(self, offset: int) -> List[Dict]:
        """
        List all records where last_update is not older than offset seconds.
        """
        if not self._validate_connection():
            logger.error("Cannot list nodes - no valid database connection")
            return []
        
        current_timestamp = int(time.time())
        cutoff_timestamp = current_timestamp - offset
        
        try:
            records = db_manager.execute_query('''
                SELECT i, name, type, key, last_update
                FROM last_seen
                WHERE last_update >= %s
                ORDER BY last_update DESC
            ''', (cutoff_timestamp,), fetch_all=True)
            
            result = []
            for record in records:
                result.append({
                    'id': record['i'],
                    'name': record['name'],
                    'type': record['type'],
                    'key': record['key'],
                    'last_update': record['last_update'],
                    'age_seconds': current_timestamp - record['last_update']
                })
            logger.debug(f"Retrieved {len(result)} active nodes (offset: {offset}s)")
            return result
        except Exception as e:
            logger.error(f"Error listing nodes: {e}")
            return []

    def get_by_key(self, instance_key: str) -> Optional[Dict]:
        """
        Get a specific node by instance key.
        """
        if not self._validate_connection():
            logger.error("Cannot get node by key - no valid database connection")
            return None
        
        try:
            record = db_manager.execute_query('''
                SELECT i, name, type, key, last_update
                FROM last_seen
                WHERE key = %s
            ''', (instance_key,), fetch_one=True)
            
            if record:
                return {
                    'id': record['i'],
                    'name': record['name'],
                    'type': record['type'],
                    'key': record['key'],
                    'last_update': record['last_update']
                }
            return None
        except Exception as e:
            logger.error(f"Error getting node by key {instance_key}: {e}")
            return None

    def delete_old_records(self, max_age_seconds: int) -> int:
        """
        Delete records older than specified age.
        """
        if not self._validate_connection():
            logger.error("Cannot delete old records - no valid database connection")
            return 0
        
        current_timestamp = int(time.time())
        cutoff_timestamp = current_timestamp - max_age_seconds
        
        try:
            deleted_count = db_manager.execute_query('''
                DELETE FROM last_seen
                WHERE last_update < %s
            ''', (cutoff_timestamp,))
            
            logger.info(f"Deleted {deleted_count} old records (older than {max_age_seconds}s)")
            return deleted_count
        except Exception as e:
            logger.error(f"Error deleting old records: {e}")
            return 0

    def get_statistics(self) -> Dict:
        """
        Get database statistics.
        """
        if not self._validate_connection():
            logger.error("Cannot get statistics - no valid database connection")
            return {}
        
        try:
            total_records = db_manager.execute_query("SELECT COUNT(*) FROM last_seen", fetch_one=True)[0]
            
            records_by_type_result = db_manager.execute_query(
                "SELECT type, COUNT(*) FROM last_seen GROUP BY type", 
                fetch_all=True
            )
            records_by_type = dict(records_by_type_result) if records_by_type_result else {}
            
            time_range = db_manager.execute_query(
                "SELECT MIN(last_update), MAX(last_update) FROM last_seen", 
                fetch_one=True
            )
            min_time, max_time = time_range if time_range else (None, None)
            
            return {
                'total_records': total_records,
                'records_by_type': records_by_type,
                'oldest_timestamp': min_time,
                'newest_timestamp': max_time,
                'current_timestamp': int(time.time())
            }
        except Exception as e:
            logger.error(f"Error getting statistics: {e}")
            return {}

    def get_object_id(self) -> Optional[int]:
        """
        Get OBJECT_ID value from cluster_state table.
        Returns the numeric value stored in field 'i' for key 'OBJECT_ID'.
        """
        if not self._validate_connection():
            logger.error("Cannot get OBJECT_ID - no valid database connection")
            return 0
        
        try:
            result = db_manager.execute_query(
                "SELECT i FROM cluster_state WHERE key = %s", 
                ('OBJECT_ID',), 
                fetch_one=True
            )
            return result[0] if result else 0
        except Exception as e:
            logger.error(f"Error getting OBJECT_ID: {e}")
            return 0

    def update_object_id(self, object_id: int) -> bool:
        """
        Update OBJECT_ID value in cluster_state table.
        Stores the numeric value in field 'i' for key 'OBJECT_ID'.
        """
        if not self._validate_connection():
            logger.error("Cannot update OBJECT_ID - no valid database connection")
            return False
        
        try:
            db_manager.execute_query('''
                INSERT INTO cluster_state (key, i) 
                VALUES (%s, %s) 
                ON CONFLICT (key) 
                DO UPDATE SET i = EXCLUDED.i
            ''', ('OBJECT_ID', object_id))
            
            logger.debug(f"Updated OBJECT_ID to: {object_id}")
            return True
        except Exception as e:
            logger.error(f"Error updating OBJECT_ID: {e}")
            return False

    def get_message_id(self) -> Optional[int]:
        """
        Get MESSAGE_ID value from cluster_state table.
        Returns the numeric value stored in field 'i' for key 'MESSAGE_ID'.
        """
        if not self._validate_connection():
            logger.error("Cannot get MESSAGE_ID - no valid database connection")
            return 0
        
        try:
            result = db_manager.execute_query(
                "SELECT i FROM cluster_state WHERE key = %s", 
                ('MESSAGE_ID',), 
                fetch_one=True
            )
            return result[0] if result else 0
        except Exception as e:
            logger.error(f"Error getting MESSAGE_ID: {e}")
            return 0

    def update_message_id(self, message_id: int) -> bool:
        """
        Update MESSAGE_ID value in cluster_state table.
        Stores the numeric value in field 'i' for key 'MESSAGE_ID'.
        """
        if not self._validate_connection():
            logger.error("Cannot update MESSAGE_ID - no valid database connection")
            return False
        
        try:
            db_manager.execute_query('''
                INSERT INTO cluster_state (key, i) 
                VALUES (%s, %s) 
                ON CONFLICT (key) 
                DO UPDATE SET i = EXCLUDED.i
            ''', ('MESSAGE_ID', message_id))
            
            logger.debug(f"Updated MESSAGE_ID to: {message_id}")
            return True
        except Exception as e:
            logger.error(f"Error updating MESSAGE_ID: {e}")
            return False

    def __del__(self):
        """Cleanup method - no longer needed as we use global database manager."""
        pass

