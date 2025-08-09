import psycopg2
import psycopg2.extras
import time
from typing import List, Dict, Optional
import logging
import threading

logger = logging.getLogger(__name__)

class DatabaseManager:
    """
    Singleton database manager for global PostgreSQL connection management.
    Provides a single connection that can be shared across the application.
    """
    _instance = None
    _lock = threading.Lock()
    db_manager = None
    
    def __new__(cls):
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = super(DatabaseManager, cls).__new__(cls)
                    cls._instance._initialized = False
                    cls.db_manager = cls._instance
        return cls._instance
    
    def __init__(self):
        if self._initialized:
            return
        
        self.connection = None
        self.conn_params = None
        self._initialized = True
        logger.info("DatabaseManager singleton created")
    
    def initialize(self, host: str, port: int, user: str, password: str, dbname: str, 
                   minconn: int = 1, maxconn: int = 5):
        """
        Initialize the database connection.
        """
        if self.connection is not None:
            logger.warning("Database connection already initialized")
            return
        
        self.conn_params = {
            'host': host,
            'port': port,
            'user': user,
            'password': password,
            'dbname': dbname,
            'connect_timeout': 10,
            'application_name': 'lunaricorn_leader',
            'options': '-c statement_timeout=30000'  # 30 second statement timeout
        }
        
        self._create_connection()
        logger.info("Database connection initialized")
    
    def _create_pool(self):
        """Create a new connection pool."""
        try:
            if self.pool:
                try:
                    self.pool.closeall()
                except Exception as close_error:
                    logger.warning(f"Error closing old pool: {close_error}")
            
            self.pool = SimpleConnectionPool(self.minconn, self.maxconn, **self.conn_params)
            logger.info("Database connection pool created successfully")
        except Exception as e:
            logger.error(f"Failed to create database connection pool: {e}")
            raise

    def reset_pool(self):
        """Reset the connection pool when there are connection issues."""
        try:
            logger.warning("Resetting database connection pool due to connection issues")
            self._create_pool()
            logger.info("Database connection pool reset successfully")
        except Exception as e:
            logger.error(f"Failed to reset database connection pool: {e}")
            raise

    def get_pool_status(self):
        """Get current connection pool status."""
        try:
            if not self.pool:
                return {"status": "not_initialized"}
            
            return {
                'minconn': self.minconn,
                'maxconn': self.maxconn,
                'pool_size': self.pool.get_size(),
                'pool_used': self.pool.get_used(),
                'status': 'active'
            }
        except Exception as e:
            logger.error(f"Error getting pool status: {e}")
            return {"status": "error", "error": str(e)}

    def get_connection_status(self):
        """Get current connection status."""
        try:
            if not self.connection:
                return {"status": "not_initialized"}
            
            return {
                'status': 'active' if not self.connection.closed else 'closed',
                'connected': not self.connection.closed
            }
        except Exception as e:
            logger.error(f"Error getting connection status: {e}")
            return {"status": "error", "error": str(e)}
    
    def shutdown(self):
        """Shutdown the database manager and close the connection."""
        try:
            if self.connection:
                self.connection.close()
                self.connection = None
                logger.info("Database connection closed")
        except Exception as e:
            logger.error(f"Error closing database connection: {e}")
    
    def __del__(self):
        """Cleanup method."""
        self.shutdown()

    def _create_connection(self):
        """Create a new database connection."""
        try:
            if self.connection:
                try:
                    self.connection.close()
                except Exception as close_error:
                    logger.warning(f"Error closing old connection: {close_error}")
            
            self.connection = psycopg2.connect(**self.conn_params)
            self.connection.autocommit = False
            logger.info("Database connection created successfully")
        except Exception as e:
            logger.error(f"Failed to create database connection: {e}")
            raise

    def reset_connection(self):
        """Reset the database connection when there are connection issues."""
        try:
            logger.warning("Resetting database connection due to connection issues")
            self._create_connection()
            logger.info("Database connection reset successfully")
        except Exception as e:
            logger.error(f"Failed to reset database connection: {e}")
            raise
    
    def get_connection(self):
        """Get the database connection."""
        if not self.connection:
            raise RuntimeError("Database connection not initialized")
        
        try:
            # Check if connection is still valid
            if self.connection.closed:
                logger.warning("Database connection is closed, reconnecting...")
                self.reset_connection()
            
            # Test the connection
            with self.connection.cursor() as cur:
                cur.execute("SELECT 1")
            
            logger.debug("Database connection is valid")
            return self.connection
        except Exception as e:
            logger.warning(f"Database connection issue detected: {e}, resetting connection...")
            self.reset_connection()
            return self.connection
    
    def return_connection(self, conn):
        """Return a connection (no-op for single connection)."""
        # No-op since we use a single connection
        logger.debug("Connection returned (single connection mode)")
    
    def validate_connection(self):
        """Validate that the database connection is working."""
        if not self.connection:
            logger.warning("Database connection is not initialized")
            return False
        
        try:
            with self.connection.cursor() as cur:
                cur.execute("SELECT 1")
            return True
        except Exception as e:
            logger.error(f"Database connection validation failed: {e}")
            return False
    
    def ensure_tables_exist(self):
        """Ensure all required tables exist in the database."""
        if not self.validate_connection():
            raise Exception("Cannot ensure tables - no valid database connection")
        
        try:
            with self.connection.cursor() as cur:
                # Create last_seen table
                cur.execute('''
                    CREATE TABLE IF NOT EXISTS last_seen (
                        i SERIAL PRIMARY KEY,
                        name VARCHAR(128) NOT NULL,
                        type VARCHAR(32) NOT NULL,
                        key VARCHAR(128) NOT NULL,
                        last_update BIGINT NOT NULL DEFAULT 0
                    )
                ''')
                
                # Create indexes for last_seen table
                cur.execute('''
                    CREATE UNIQUE INDEX IF NOT EXISTS idx_last_seen_key ON last_seen(key)
                ''')
                cur.execute('''
                    CREATE INDEX IF NOT EXISTS idx_last_seen_last_update ON last_seen(last_update)
                ''')
                cur.execute('''
                    CREATE INDEX IF NOT EXISTS idx_last_seen_type_last_update ON last_seen(type, last_update)
                ''')
                
                # Create cluster_state table
                cur.execute('''
                    CREATE TABLE IF NOT EXISTS cluster_state
                    (
                        key character varying(64) NOT NULL,
                        i bigint DEFAULT NULL,
                        j jsonb DEFAULT NULL,
                        PRIMARY KEY (key)
                    );
                ''')
                cur.execute('''
                    ALTER TABLE IF EXISTS cluster_state OWNER to lunaricorn;
                ''')
                
                logger.info("Ensured all required tables and indexes exist")
            self.connection.commit()
        except Exception as e:
            logger.error(f"Error ensuring tables exist: {e}")
            raise
    
    def execute_query(self, query: str, params: tuple = None, fetch_one: bool = False, fetch_all: bool = False):
        """
        Execute a database query with automatic connection management.
        
        Args:
            query: SQL query to execute
            params: Query parameters
            fetch_one: Whether to fetch one result
            fetch_all: Whether to fetch all results
            
        Returns:
            Query result or None
        """
        try:
            # Get and validate connection
            conn = self.get_connection()
            
            # Validate connection before use
            if conn is None or conn.closed:
                raise RuntimeError("Invalid or closed connection")
            
            with conn.cursor(cursor_factory=psycopg2.extras.DictCursor) as cur:
                cur.execute(query, params)
                
                if fetch_one:
                    return cur.fetchone()
                elif fetch_all:
                    return cur.fetchall()
                else:
                    conn.commit()
                    return cur.rowcount
        except Exception as e:
            logger.error(f"Error executing query: {e}")
            if self.connection and not self.connection.closed:
                try:
                    self.connection.rollback()
                except Exception as rollback_error:
                    logger.debug(f"Error during rollback: {rollback_error}")
            raise


# Global instance
