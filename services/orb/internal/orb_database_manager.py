from lunaricorn.utils.db_manager import *

class OrbDatabaseManager(DatabaseManager):
    def installer_impl(self, cur):
        cur.execute('''
            CREATE TABLE IF NOT EXISTS public.orb_meta
            (
                id bigserial NOT NULL,
                data_type character varying(64) NOT NULL DEFAULT '@json',
                ctime timestamp without time zone NOT NULL,
                flags jsonb NOT NULL DEFAULT '[]'::JSONB,
                src bigint NOT NULL,
                PRIMARY KEY (id)
            );
            ''')
        
        cur.execute('''ALTER TABLE IF EXISTS public.orb_meta OWNER to lunaricorn''')
        cur.execute('''
            CREATE INDEX IF NOT EXISTS idata_type
                ON public.orb_meta USING btree
                (data_type)
                WITH (deduplicate_items=True)
            ;
            ''')
        cur.execute('''ALTER TABLE IF EXISTS public.orb_meta CLUSTER ON idata_type;''')

