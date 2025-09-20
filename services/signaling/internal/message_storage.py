from lunaricorn.utils.db_manager import *
from .data_types import *
from .signaling_database_manager import *
from datetime import datetime
import json
import time as time_module
from lunaricorn.utils.db_manager import *

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

    def get_unique_values(self, field_name):
        if field_name not in ['type', 'affected', 'owner', 'tags']:
            raise ValueError(f"list operation for field {field_name} not supported")

        if field_name in ['type', 'owner']:
            query = f"""
                SELECT DISTINCT {field_name} as f
                FROM public.signaling_events
                WHERE {field_name} IS NOT NULL
                ORDER BY f ASC
            """
            result = self.db_manager.execute_query(query=query, params=None, fetch_one = False,fetch_all = True)
            return [row[0] for row in result]


        elif field_name == 'tags':
            query = """
                SELECT DISTINCT UNNEST(tags) as tag
                FROM public.signaling_events
                WHERE tags IS NOT NULL AND array_length(tags, 1) > 0
                ORDER BY tag ASC
            """
            result = self.db_manager.execute_query(query=query, params=None, fetch_one = False,fetch_all = True)
            return [row[0] for row in result]

        elif field_name == 'affected':
            query = """
                SELECT DISTINCT jsonb_array_elements_text(affected) AS affected_value
                FROM public.signaling_events
                WHERE affected IS NOT NULL 
                AND affected != 'null'::jsonb
                AND jsonb_typeof(affected) = 'array'
                ORDER BY affected_value ASC
            """
            result = self.db_manager.execute_query(query=query, params=None, fetch_one = False,fetch_all = True)
            return [row[0] for row in result]
        
    def find_events(self, timestamp:float, types:list = [], sources:list = [], affected:list = [], tags:list = [], limit:int = 0) -> list:
        conditions = ["ctime >= %s"]
        params = [datetime.fromtimestamp(timestamp)]

        if types:
            placeholders = ",".join(["%s"] * len(types))
            conditions.append(f"type IN ({placeholders})")
            params.extend(types)

        if sources:
            placeholders = ",".join(["%s"] * len(sources))
            conditions.append(f"owner IN ({placeholders})")
            params.extend(sources)

        if affected:
            placeholders = ",".join(["%s"] * len(affected))
            conditions.append(f"affected @> ANY(ARRAY[{placeholders}]::jsonb[])")
            params.extend([f'["{a}"]' for a in affected])

        if tags:
            placeholders = ",".join(["%s"] * len(tags))
            conditions.append(f"tags && ARRAY[{placeholders}]")
            params.extend(tags)


        query = f"""
            SELECT eid, type, payload, affected, ctime, owner, tags
            FROM public.signaling_events 
            WHERE {' AND '.join(conditions)}
            ORDER BY ctime DESC
        """
        if limit > 0:
            query += f" LIMIT {limit}"

        start_time = time_module.perf_counter()
        result = self.db_manager.execute_query(
            query=query,
            params=params,
            fetch_one = False,
            fetch_all = True
        )
        end_time = time_module.perf_counter()
        execution_time = end_time - start_time
        self.logger.info(f"ROBOTS: find_events query - {execution_time} - count: {len(result)} q: {Dbutils.compress_query(query)}, params: {params}")
        events = self.__result_to_event_data_extended_list(result)
        #for event in events:
        #    self.logger.info(f"EID: {event.eid}, Type: {event.event_type}, Timestamp: {event.timestamp}")
        return events

    def __result_to_event_data_extended_list(self, data_list: list) -> List[EventDataExtended]:
        result = []
        for item in data_list:
            event_dict = {
                'eid': item[0],
                'type': item[1],
                'payload': item[2],
                'affected': item[3],
                'ctime': item[4],
                'owner': item[5],
                'tags': item[6]
            }
            event_extended = EventDataExtended.from_dict(event_dict)
            result.append(event_extended)
        
        return result


    def find_events_by_type(self, event_type: str) -> List[EventDataExtended]:
        """
        Find events by type and return them as EventDataExtended objects
        
        Args:
            event_type: The type of events to search for
            
        Returns:
            List of EventDataExtended objects matching the criteria
        """
        query = """
            SELECT eid, type, payload, affected, ctime, owner, tags
            FROM public.signaling_events 
            WHERE type = %s
            ORDER BY ctime DESC;
        """
        
        params = (event_type,)
        
        try:
            results = self.db_manager.execute_query(
                query=query,
                params=params,
                fetch_all=True
            )
            
            events = []
            for row in results:
                # Convert database row to EventDataExtended object
                eid, db_type, payload_json, affected_json, ctime, owner, tags = row
                
                # Parse JSON fields
                payload = json.loads(payload_json) if payload_json else {}
                affected = json.loads(affected_json) if affected_json else None
                
                # Convert datetime to timestamp
                timestamp = ctime.timestamp() if ctime else 0
                
                # Determine source (convert "ownerless" to None)
                source = owner if owner != "ownerless" else None
                
                event = EventDataExtended(
                    eid=eid,
                    event_type=db_type,
                    payload=payload,
                    timestamp=timestamp,
                    source=source,
                    affected=affected,
                    tags=tags
                )
                events.append(event)
            
            return events
            
        except Exception as e:
            self.logger.error(f"Error searching for events of type '{event_type}': {e}")
            return []
