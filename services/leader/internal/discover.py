import sqlite3
import time
from typing import List, Dict, Optional
from pathlib import Path
import logging

logger = logging.getLogger(__name__)

class DiscoverManager:
    
    # Static constant for database path
    DB_PATH = "/opt/lunaricorn/leader_data/known_nodes.db"
    
    def __init__(self):
        """
        Initialize the manager.
        Checks for database file existence, establishes connection, and performs vacuum.
        """
        self.db_path = Path(self.DB_PATH)
        self._check_database_exists()
        self._establish_connection()
        self._vacuum_database()
    
    def _check_database_exists(self):
        """Check if database file exists."""
        if not self.db_path.exists():
            raise FileNotFoundError(f"Database file not found: {self.db_path}")
        logger.info(f"Database file found: {self.db_path}")
    
    def _establish_connection(self):
        """Establish database connection."""
        try:
            self.connection = sqlite3.connect(self.db_path)
            self.connection.row_factory = sqlite3.Row  # Enable dict-like access
            logger.info("Database connection established")
        except Exception as e:
            logger.error(f"Error establishing database connection: {e}")
            raise
    
    def _validate_connection(self):
        """Validate database connection is active."""
        try:
            # Try a simple query to check connection
            self.connection.execute("SELECT 1")
            return True
        except (sqlite3.OperationalError, sqlite3.DatabaseError, AttributeError):
            logger.warning("Database connection lost, attempting to reconnect")
            try:
                self._establish_connection()
                return True
            except Exception as e:
                logger.error(f"Failed to reconnect to database: {e}")
                return False
    
    def _vacuum_database(self):
        """Perform database vacuum to optimize storage."""
        if not self._validate_connection():
            raise Exception("Cannot perform vacuum - no valid database connection")
        
        try:
            self.connection.execute("VACUUM")
            self.connection.commit()
            logger.info("Database vacuum completed")
        except Exception as e:
            logger.error(f"Error during database vacuum: {e}")
            raise
    
    def update(self, node_name: str, node_type: str, instance_key: str, host: Optional[str] = None, port: Optional[int] = 0) -> bool:
        """
        Update node information in the database.
        If record exists with same instance_key, update fields and timestamp.
        If not exists, create new record.
        
        Args:
            node_name: Name of the node
            node_type: Type of the node
            instance_key: Unique instance identifier
            host: Host address (optional)
            port: Port number (optional, defaults to 0)
            
        Returns:
            bool: True if successful, False otherwise
        """
        if not self._validate_connection():
            logger.error("Cannot update node - no valid database connection")
            return False
        
        current_timestamp = int(time.time())
        
        try:
            cursor = self.connection.cursor()
            
            # Check if record exists with this instance_key
            cursor.execute(
                "SELECT i FROM last_seen WHERE key = ?",
                (instance_key,)
            )
            existing_record = cursor.fetchone()
            
            if existing_record:
                # Update existing record
                cursor.execute("""
                    UPDATE last_seen 
                    SET name = ?, type = ?, last_update = ?, host = ?, port = ?
                    WHERE key = ?
                """, (node_name, node_type, current_timestamp, host, port, instance_key))
                logger.debug(f"Updated existing node: {node_name} ({instance_key})")
            else:
                # Insert new record
                cursor.execute("""
                    INSERT INTO last_seen (name, type, key, last_update, host, port)
                    VALUES (?, ?, ?, ?, ?, ?)
                """, (node_name, node_type, instance_key, current_timestamp, host, port))
                logger.debug(f"Added new node: {node_name} ({instance_key})")
            
            self.connection.commit()
            return True
            
        except Exception as e:
            logger.error(f"Error updating node {node_name}: {e}")
            return False
    
    def list(self, offset: int) -> List[Dict]:
        """
        List all records where last_update is not older than offset seconds.
        
        Args:
            offset: Maximum age in seconds for records to be included
            
        Returns:
            List of dictionaries containing node information
        """
        if not self._validate_connection():
            logger.error("Cannot list nodes - no valid database connection")
            return []
        
        current_timestamp = int(time.time())
        cutoff_timestamp = current_timestamp - offset
        
        try:
            cursor = self.connection.cursor()
            cursor.execute("""
                SELECT i, name, type, key, last_update, host, port
                FROM last_seen
                WHERE last_update >= ?
                ORDER BY last_update DESC
            """, (cutoff_timestamp,))
            
            records = cursor.fetchall()
            
            result = []
            for record in records:
                result.append({
                    'id': record[0],
                    'name': record[1],
                    'type': record[2],
                    'key': record[3],
                    'last_update': record[4],
                    'host': record[5],
                    'port': record[6],
                    'age_seconds': current_timestamp - record[4]
                })
            
            logger.debug(f"Retrieved {len(result)} active nodes (offset: {offset}s)")
            return result
            
        except Exception as e:
            logger.error(f"Error listing nodes: {e}")
            return []
    
    def get_by_key(self, instance_key: str) -> Optional[Dict]:
        """
        Get a specific node by instance key.
        
        Args:
            instance_key: Unique instance identifier
            
        Returns:
            Dictionary with node information or None if not found
        """
        if not self._validate_connection():
            logger.error("Cannot get node by key - no valid database connection")
            return None
        
        try:
            cursor = self.connection.cursor()
            cursor.execute("""
                SELECT i, name, type, key, last_update, host, port
                FROM last_seen
                WHERE key = ?
            """, (instance_key,))
            
            record = cursor.fetchone()
            
            if record:
                return {
                    'id': record[0],
                    'name': record[1],
                    'type': record[2],
                    'key': record[3],
                    'last_update': record[4],
                    'host': record[5],
                    'port': record[6]
                }
            return None
            
        except Exception as e:
            logger.error(f"Error getting node by key {instance_key}: {e}")
            return None
    
    def delete_old_records(self, max_age_seconds: int) -> int:
        """
        Delete records older than specified age.
        
        Args:
            max_age_seconds: Maximum age in seconds for records to keep
            
        Returns:
            Number of deleted records
        """
        if not self._validate_connection():
            logger.error("Cannot delete old records - no valid database connection")
            return 0
        
        current_timestamp = int(time.time())
        cutoff_timestamp = current_timestamp - max_age_seconds
        
        try:
            cursor = self.connection.cursor()
            cursor.execute("""
                DELETE FROM last_seen
                WHERE last_update < ?
            """, (cutoff_timestamp,))
            
            deleted_count = cursor.rowcount
            self.connection.commit()
            
            if deleted_count > 0:
                logger.info(f"Deleted {deleted_count} old records (older than {max_age_seconds}s)")
            
            return deleted_count
            
        except Exception as e:
            logger.error(f"Error deleting old records: {e}")
            return 0
    
    def get_statistics(self) -> Dict:
        """
        Get database statistics.
        
        Returns:
            Dictionary with statistics
        """
        if not self._validate_connection():
            logger.error("Cannot get statistics - no valid database connection")
            return {}
        
        try:
            cursor = self.connection.cursor()
            
            # Total records
            cursor.execute("SELECT COUNT(*) FROM last_seen")
            total_records = cursor.fetchone()[0]
            
            # Records by type
            cursor.execute("""
                SELECT type, COUNT(*) 
                FROM last_seen 
                GROUP BY type
            """)
            records_by_type = dict(cursor.fetchall())
            
            # Oldest and newest timestamps
            cursor.execute("""
                SELECT MIN(last_update), MAX(last_update)
                FROM last_seen
            """)
            min_time, max_time = cursor.fetchone()
            
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
    
    def __del__(self):
        """Cleanup method to close database connection."""
        try:
            if hasattr(self, 'connection'):
                self.connection.close()
                logger.info("Database connection closed")
        except Exception as e:
            logger.error(f"Error closing database connection: {e}")
