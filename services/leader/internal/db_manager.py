import psycopg2
import psycopg2.extras
from psycopg2.pool import SimpleConnectionPool
import time
from typing import List, Dict, Optional
import logging
import threading

logger = logging.getLogger(__name__)

class DatabaseManager:
    """
    Singleton database manager for global PostgreSQL connection management.
    Provides a single connection pool that can be shared across the application.
    """
    _instance = None
    _lock = threading.Lock()
    
    def __new__(cls):
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = super(DatabaseManager, cls).__new__(cls)
                    cls._instance._initialized = False
        return cls._instance
    
    def __init__(self):
        if self._initialized:
            return
        
        self.pool = None
        self.conn_params = None
        self.minconn = 1
        self.maxconn = 5
        self._initialized = True
        logger.info("DatabaseManager singleton created")
    
    def initialize(self, host: str, port: int, user: str, password: str, dbname: str, 
                   minconn: int = 1, maxconn: int = 5):
        """
        Initialize the database connection pool.
        """
        if self.pool is not None:
            logger.warning("Database pool already initialized")
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
        self.minconn = minconn
        self.maxconn = maxconn
        
        self._create_pool()
        logger.info(f"Database pool initialized with {minconn}-{maxconn} connections")
    
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
    
    def get_connection(self):
        """Get a connection from the pool."""
        if not self.pool:
            raise RuntimeError("Database pool not initialized")
        
        try:
            conn = self.pool.getconn()
            if conn is None:
                raise RuntimeError("Received None connection from pool")
            logger.debug("Connection acquired from pool")
            return conn
        except Exception as e:
            if "connection pool is closed" in str(e).lower() or "connection pool exhausted" in str(e).lower() or "unkeyed connection" in str(e).lower():
                logger.warning(f"Connection pool issue detected: {e}, resetting pool...")
                self.reset_pool()
                conn = self.pool.getconn()
                if conn is None:
                    raise RuntimeError("Received None connection from reset pool")
                logger.debug("Connection acquired from reset pool")
                return conn
            else:
                logger.error(f"Failed to get connection from pool: {e}")
                raise
    
    def return_connection(self, conn):
        """Return a connection to the pool."""
        if conn is None:
            logger.warning("Attempted to return None connection to pool")
            return
        
        if not self.pool:
            logger.warning("Attempted to return connection to uninitialized pool")
            try:
                conn.close()
            except:
                pass
            return
        
        try:
            # Check if connection is still valid before returning
            if conn.closed:
                logger.warning("Attempted to return closed connection to pool")
                return
            
            self.pool.putconn(conn)
            logger.debug("Connection returned to pool")
        except Exception as e:
            logger.error(f"Failed to return connection to pool: {e}")
            # Try to close the connection if we can't return it to the pool
            try:
                if conn and not conn.closed:
                    conn.close()
                    logger.debug("Connection closed due to pool return failure")
            except Exception as close_error:
                logger.debug(f"Error closing connection: {close_error}")
    
    def validate_connection(self):
        """Validate that a connection can be acquired from the pool."""
        if not self.pool:
            logger.warning("Database pool is not initialized")
            return False
        
        conn = None
        try:
            conn = self.get_connection()
            with conn.cursor() as cur:
                cur.execute("SELECT 1")
            return True
        except Exception as e:
            logger.error(f"Database connection validation failed: {e}")
            return False
        finally:
            if conn:
                self.return_connection(conn)
    
    def ensure_tables_exist(self):
        """Ensure all required tables exist in the database."""
        if not self.validate_connection():
            raise Exception("Cannot ensure tables - no valid database connection")
        
        conn = self.get_connection()
        try:
            with conn.cursor() as cur:
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
            conn.commit()
        except Exception as e:
            logger.error(f"Error ensuring tables exist: {e}")
            raise
        finally:
            self.return_connection(conn)
    
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
        conn = None
        try:
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
            if conn and not conn.closed:
                try:
                    conn.rollback()
                except Exception as rollback_error:
                    logger.debug(f"Error during rollback: {rollback_error}")
            raise
        finally:
            if conn:
                self.return_connection(conn)
    
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
    
    def shutdown(self):
        """Shutdown the database manager and close all connections."""
        try:
            if self.pool:
                self.pool.closeall()
                self.pool = None
                logger.info("Database connection pool closed")
        except Exception as e:
            logger.error(f"Error closing database connection pool: {e}")
    
    def __del__(self):
        """Cleanup method."""
        self.shutdown()


# Global instance
db_manager = DatabaseManager() 