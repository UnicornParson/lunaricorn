from lunaricorn.utils.db_manager import *

class OrbDatabaseManager(DatabaseManager):
    def __init__(self):
        super().__init__()
        self._last_cursor_description = None

    def get_last_cursor_description(self):
        """Get the description of the last cursor used in execute_query"""
        return self._last_cursor_description
    
    def installer_impl(self, cur):
        # META
        cur.execute('''
            CREATE TABLE IF NOT EXISTS public.orb_meta
            (
                id bigserial NOT NULL,
                u UUID DEFAULT uuidv7(),
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
        cur.execute('''
            CREATE INDEX IF NOT EXISTS iu
                ON public.orb_meta USING btree
                (u)
                WITH (deduplicate_items=True)
            ;
            ''')
        cur.execute('''ALTER TABLE IF EXISTS public.orb_meta CLUSTER ON idata_type;''')

        # DATA
        cur.execute('''
            CREATE TABLE IF NOT EXISTS public.orb_data
            (
                u uuid NOT NULL DEFAULT uuidv7(),
                data_type character varying(256) NOT NULL DEFAULT '@json',
                chain_left uuid,
                chain_right uuid,
                parent uuid,
                ctime timestamp without time zone NOT NULL,
                flags jsonb NOT NULL DEFAULT '[]'::JSONB,
                src text,
                data jsonb,
                PRIMARY KEY (u)
            );
        ''')
        cur.execute('''ALTER TABLE IF EXISTS public.orb_data OWNER to lunaricorn;''')
