from lunaricorn.utils.db_manager import DatabaseManager
import logging
import threading

logger = logging.getLogger(__name__)

class LeaderDatabaseManager(DatabaseManager):
    def __init__(self):
        super().__init__()
        self._last_cursor_description = None

    def get_last_cursor_description(self):
        return self._last_cursor_description
    
    def installer_impl(self, cur):
        logger.info("@@ LeaderDatabaseManager installer_impl")
        cur.execute('''
            CREATE TABLE IF NOT EXISTS last_seen (
                i SERIAL PRIMARY KEY,
                name VARCHAR(128) NOT NULL,
                type VARCHAR(32) NOT NULL,
                key VARCHAR(128) NOT NULL,
                last_update BIGINT NOT NULL DEFAULT 0
            )''')

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