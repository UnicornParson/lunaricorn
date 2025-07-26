import psycopg2
import psycopg2.extras
from psycopg2.pool import SimpleConnectionPool
import time
from typing import List, Dict, Optional
import logging

logger = logging.getLogger(__name__)

class DiscoverManagerPG:
    """
    Manager for discovering and updating node information in a PostgreSQL database using a connection pool.
    """
    def __init__(self, host: str, port: int, user: str, password: str, dbname: str, minconn: int = 1, maxconn: int = 5):
        """
        Initialize the manager.
        Establishes a connection pool and ensures the table exists.
        """
        self.conn_params = {
            'host': host,
            'port': port,
            'user': user,
            'password': password,
            'dbname': dbname
        }
        self.pool = SimpleConnectionPool(minconn, maxconn, **self.conn_params)
        logger.info("PostgreSQL connection pool created")
        self._ensure_table_exists()

    def _get_conn(self):
        """Get a connection from the pool."""
        return self.pool.getconn()

    def _put_conn(self, conn):
        """Return a connection to the pool."""
        self.pool.putconn(conn)

    def _validate_connection(self):
        """Validate that a connection can be acquired from the pool."""
        try:
            conn = self._get_conn()
            with conn.cursor() as cur:
                cur.execute("SELECT 1")
            self._put_conn(conn)
            return True
        except Exception as e:
            logger.error(f"Connection pool validation failed: {e}")
            return False

    def _ensure_table_exists(self):
        """Create the last_seen table if it does not exist."""
        if not self._validate_connection():
            raise Exception("Cannot ensure table - no valid database connection")
        conn = self._get_conn()
        try:
            with conn.cursor() as cur:
                cur.execute('''
                    CREATE TABLE IF NOT EXISTS last_seen (
                        i SERIAL PRIMARY KEY,
                        name VARCHAR(128) NOT NULL,
                        type VARCHAR(32) NOT NULL,
                        key VARCHAR(128) NOT NULL,
                        last_update BIGINT NOT NULL DEFAULT 0
                    )
                ''')
                # Create unique index on key
                cur.execute('''
                    CREATE UNIQUE INDEX IF NOT EXISTS idx_last_seen_key ON last_seen(key)
                ''')
                # Create index on last_update
                cur.execute('''
                    CREATE INDEX IF NOT EXISTS idx_last_seen_last_update ON last_seen(last_update)
                ''')
                # Create composite index on (type, last_update)
                cur.execute('''
                    CREATE INDEX IF NOT EXISTS idx_last_seen_type_last_update ON last_seen(type, last_update)
                ''')
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
                    ALTER TABLE IF EXISTS cluster_state  OWNER to lunaricorn;
                ''')

                logger.info("Ensured last_seen table and indexes exist")
            conn.commit()
        except Exception as e:
            logger.error(f"Error ensuring table exists: {e}")
            raise
        finally:
            self._put_conn(conn)

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
        conn = self._get_conn()
        try:
            with conn.cursor() as cur:
                cur.execute(
                    "SELECT i FROM last_seen WHERE key = %s",
                    (instance_key,)
                )
                existing_record = cur.fetchone()
                if existing_record:
                    cur.execute('''
                        UPDATE last_seen 
                        SET name = %s, type = %s, last_update = %s, key = %s
                        WHERE key = %s
                    ''', (node_name, node_type, current_timestamp, instance_key, instance_key))
                    logger.debug(f"Updated existing node: {node_name} ({instance_key})")
                else:
                    cur.execute('''
                        INSERT INTO last_seen (name, type, key, last_update)
                        VALUES (%s, %s, %s, %s)
                    ''', (node_name, node_type, instance_key, current_timestamp))
                    logger.debug(f"Added new node: {node_name} ({instance_key})")
            conn.commit()
            return True
        except Exception as e:
            logger.error(f"Error updating node {node_name}: {e}")
            return False
        finally:
            self._put_conn(conn)

    def list(self, offset: int) -> List[Dict]:
        """
        List all records where last_update is not older than offset seconds.
        """
        if not self._validate_connection():
            logger.error("Cannot list nodes - no valid database connection")
            return []
        current_timestamp = int(time.time())
        cutoff_timestamp = current_timestamp - offset
        conn = self._get_conn()
        try:
            with conn.cursor(cursor_factory=psycopg2.extras.DictCursor) as cur:
                cur.execute('''
                    SELECT i, name, type, key, last_update
                    FROM last_seen
                    WHERE last_update >= %s
                    ORDER BY last_update DESC
                ''', (cutoff_timestamp,))
                records = cur.fetchall()
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
        finally:
            self._put_conn(conn)

    def get_by_key(self, instance_key: str) -> Optional[Dict]:
        """
        Get a specific node by instance key.
        """
        if not self._validate_connection():
            logger.error("Cannot get node by key - no valid database connection")
            return None
        conn = self._get_conn()
        try:
            with conn.cursor(cursor_factory=psycopg2.extras.DictCursor) as cur:
                cur.execute('''
                    SELECT i, name, type, key, last_update
                    FROM last_seen
                    WHERE key = %s
                ''', (instance_key,))
                record = cur.fetchone()
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
        finally:
            self._put_conn(conn)

    def delete_old_records(self, max_age_seconds: int) -> int:
        """
        Delete records older than specified age.
        """
        if not self._validate_connection():
            logger.error("Cannot delete old records - no valid database connection")
            return 0
        current_timestamp = int(time.time())
        cutoff_timestamp = current_timestamp - max_age_seconds
        conn = self._get_conn()
        try:
            with conn.cursor() as cur:
                cur.execute('''
                    DELETE FROM last_seen
                    WHERE last_update < %s
                ''', (cutoff_timestamp,))
                deleted_count = cur.rowcount
                logger.info(f"Deleted {deleted_count} old records (older than {max_age_seconds}s)")
                conn.commit()
                return deleted_count
        except Exception as e:
            logger.error(f"Error deleting old records: {e}")
            return 0
        finally:
            self._put_conn(conn)

    def get_statistics(self) -> Dict:
        """
        Get database statistics.
        """
        if not self._validate_connection():
            logger.error("Cannot get statistics - no valid database connection")
            return {}
        conn = self._get_conn()
        try:
            with conn.cursor() as cur:
                cur.execute("SELECT COUNT(*) FROM last_seen")
                total_records = cur.fetchone()[0]
                cur.execute("SELECT type, COUNT(*) FROM last_seen GROUP BY type")
                records_by_type = dict(cur.fetchall())
                cur.execute("SELECT MIN(last_update), MAX(last_update) FROM last_seen")
                min_time, max_time = cur.fetchone()
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
        finally:
            self._put_conn(conn)

    def __del__(self):
        """Cleanup method to close the connection pool."""
        try:
            if hasattr(self, 'pool'):
                self.pool.closeall()
                logger.info("PostgreSQL connection pool closed")
        except Exception as e:
            logger.error(f"Error closing PostgreSQL connection pool: {e}")
