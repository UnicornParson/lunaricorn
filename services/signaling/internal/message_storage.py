from lunaricorn.utils.db_manager import *
from .data_types import *
from .signaling_database_manager import *
from datetime import datetime
import json
import time as time_module


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
        
    def find_events(self, timestamp:float, types:list = [], sources:list = [], affected:list = [], tags:list = [], limit:int = 0) -> list:
        types_q = " "
        sources_q = " "
        affected_q = " "
        tags_q = " "
        limit_q = " "

        if limit > 0:
            limit_q = f"LIMIT {int(limit)}"

        if types:
            tlist = ', '.join(f"'{s}'" for s in types)
            types_q = f"AND (type IN ({tlist}) )"
        if sources:
            slist = ', '.join(f"'{s}'" for s in sources)
            sources_q = f"AND (type IN ({slist}) )"
        if affected:
            alist = ', '.join(f"'{s}'" for s in affected)
            affected_q = f"AND (type IN ({alist}) )"
        if tags:
            tlist = ', '.join(f"'{s}'" for s in tags)
            tags_q = f"AND (type IN ({tlist}) )"

        query = f"""
            SELECT eid, type, payload, affected, ctime, owner, tags
            FROM public.signaling_events 
            WHERE ((ctime >= %s) {types_q} {sources_q} {affected_q} {tags_q})
            ORDER BY ctime DESC
            {limit_q};
        """
        print(f"run q {query}")

        params = (
            datetime.fromtimestamp(timestamp),
        )
        start_time = time_module.perf_counter()
        result = self.db_manager.execute_query(
            query=query,
            params=params,
            fetch_one = False,
            fetch_all = True
        )
        end_time = time_module.perf_counter()
        execution_time = end_time - start_time
        self.logger.info(f"ROBOTS: find_events query - {execution_time} - count: {len(result)}")
        events = self.__result_to_event_data_extended_list(result)
        for event in events:
            self.logger.info(f"EID: {event.eid}, Type: {event.event_type}, Timestamp: {event.timestamp}")
        return events

    def __result_to_event_data_extended_list(self, data_list: list) -> List[EventDataExtended]:
        result = []
        for item in data_list:
            # Создаем словарь с соответствующими ключами
            event_dict = {
                'eid': item[0],
                'type': item[1],
                'payload': item[2],
                'affected': item[3],
                'ctime': item[4],
                'owner': item[5],
                'tags': item[6]
            }
            
            # Создаем объект EventDataExtended
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
