from lunaricorn.utils.db_manager import *

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