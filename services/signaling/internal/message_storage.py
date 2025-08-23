from lunaricorn.utils.db_manager import *
from .data_types import *
from datetime import datetime
import json

class SignalingDatabaseManager(DatabaseManager):
    def installer_impl(self, cur):
        cur.execute('''
            CREATE TABLE IF NOT EXISTS public.signaling_events
            (
                eid bigserial PRIMARY KEY,
                type character varying(256) NOT NULL,
                payload jsonb,
                affected jsonb,
                ctime timestamp without time zone NOT NULL DEFAULT now(),
                owner character varying(256) NOT NULL,
                tags text[]
            ) WITH ( autovacuum_enabled = TRUE) TABLESPACE pg_default;
            ''')
        
        cur.execute('''ALTER TABLE IF EXISTS public.signaling_events OWNER to lunaricorn''')

        cur.execute('''
            CREATE INDEX IF NOT EXISTS "byCtime"
                ON public.signaling_events USING btree
                (ctime ASC NULLS LAST)
                WITH (deduplicate_items=True)
                TABLESPACE pg_default;
            ''')
                    
        cur.execute('''
            CREATE INDEX IF NOT EXISTS "byOwner"
                ON public.signaling_events USING btree
                (owner COLLATE pg_catalog."default" ASC NULLS LAST)
                WITH (deduplicate_items=True)
                TABLESPACE pg_default;
            ''')

        cur.execute('''
            CREATE INDEX IF NOT EXISTS "byTags"
                ON public.signaling_events USING gin
                (tags COLLATE pg_catalog."default")
                WITH (fastupdate=True)
                TABLESPACE pg_default;
            ''')

        cur.execute('''
            CREATE INDEX IF NOT EXISTS "byType"
                ON public.signaling_events USING btree
                (type COLLATE pg_catalog."default" ASC NULLS LAST)
                INCLUDE(type)
                WITH (deduplicate_items=True)
                TABLESPACE pg_default;
            ''')

class StorageError(Exception):
    pass

class BrokenStorageError(Exception):
    def __init__(self, message):
        self.message = message
        super().__init__(self.message)
    
    def __str__(self):
        return f"BrokenStorageError: {self.message}"


class MessageStorage:
    def __init__(self, db_cfg: DbConfig):
        self.db_cfg = db_cfg
        self.ready = False
        self.logger = logging.getLogger()
        if not db_cfg.valid():
            raise ValueError("invalid db config")
        try:
            db_manager = SignalingDatabaseManager()
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
            self.logger.info("Database connection initialized for signaling service")

        except Exception as e:
            self.logger.error(f"Failed to initialize database connection: {e}")
            self.db_enabled = False
            raise BrokenStorageError(f"cannot init storage. reason: {e}")
            


    def create_event(self, event_data: EventData) -> int:
        ctime = datetime.fromtimestamp(event_data.timestamp)
        owner = event_data.source if event_data.source else "ownerless"

        query = """
            INSERT INTO public.signaling_events 
            (type, payload, affected, ctime, owner, tags)
            VALUES (%s, %s, %s, %s, %s, %s)
            RETURNING eid;
        """
        payload_json = json.dumps(event_data.payload) if event_data.payload else None
        affected_json = json.dumps(event_data.affected) if event_data.affected else None
        params = (
            event_data.event_type,
            payload_json,
            affected_json,
            ctime,
            owner,
            event_data.tags
        )
        
        result = self.db_manager.execute_query(
            query=query,
            params=params,
            fetch_one=True
        )
        
        return result[0]
        


