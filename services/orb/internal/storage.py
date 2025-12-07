import json
import re
import uuid
from lunaricorn.utils.db_manager import *
import lunaricorn.api.signaling as lsig
import logging
from datetime import datetime, timezone
from typing import List, Dict, Any, Optional
from .orb_database_manager import *
from .orb_types import *

class StorageError(Exception):
    pass

class BrokenStorageError(Exception):
    def __init__(self, message):
        self.message = message
        super().__init__(self.message)

    def __str__(self):
        return f"BrokenStorageError: {self.message}"

class DataStorage:
    def __init__(self, db_cfg: OrbConfig):
        self.db_cfg = db_cfg
        self.ready = False
        self.logger = logging.getLogger(__name__)
        if not db_cfg.valid():
            raise ValueError("invalid db config")
        try:
            # Initialize database connection
            db_manager = OrbDatabaseManager()
            db_manager.initialize(
                host=db_cfg.db_host,
                port=db_cfg.db_port,
                user=db_cfg.db_user,
                password=db_cfg.db_password,
                dbname=db_cfg.db_name,
                minconn=1,
                maxconn=3
            )

            db_manager.install_db()
            self.db_manager = db_manager
            self.db_enabled = True
            self.signaling_enabled = False
            self.agent_id = "ORB"
            self.logger.info("Database connection initialized for orb service")
            self.sig_cfg = lsig.SignalingClientConfig(db_cfg.SIGNALING_HOST, db_cfg.SIGNALING_REQ, db_cfg.SIGNALING_PUB, db_cfg.SIGNALING_API)
            self.logger.info(f"connect to signaling {str(self.sig_cfg)}")


            self.sig_client = lsig.SignalingClient(self.sig_cfg, self.agent_id)
            rc = self.sig_client.connect()
            if not rc:
                self.sig_client.disconnect()
                self.sig_client = None
                raise ConnectionError(f"cannot connect to signaling server {str(self.sig_config)}")

            self.signaling_enabled = True
            self.ready = True

        except Exception as e:
            self.logger.error(f"Failed to initialize database connection: {e}")
            self.db_enabled = False
            raise BrokenStorageError(f"cannot init storage. reason: {e}")

    def good(self) -> bool:
        return self.db_enabled and self.signaling_enabled and self.ready

    def _prepare_data_for_db(self, data: Dict[str, Any]) -> Dict[str, Any]:
        """Prepare data for database insertion, converting lists/dicts to JSON strings for jsonb columns"""
        prepared_data = {}
        for key, value in data.items():
            if isinstance(value, (list, dict)):
                prepared_data[key] = json.dumps(value)
            elif isinstance(value, uuid.UUID):
                # Convert UUID to string for database storage
                prepared_data[key] = str(value)
            else:
                prepared_data[key] = value
        return prepared_data

    def _execute_query_with_columns(self, query: str, params: tuple = None, columns: List[str] = None) -> List[Dict[str, Any]]:
        self.logger.debug(f"@@ Execute a query and return results with column names \n q: \n{query}\n params: \n{params}\n columns: \n{columns}")
        result = self.db_manager.execute_query(
            query=query,
            params=params,
            fetch_one=False,
            fetch_all=True
        )

        if result and columns:
            return [dict(zip(columns, row)) for row in result]

        return []

    def create_record(self, table_name: str, data: Dict[str, Any], id_field="id") -> int:
        """Create a new record in the specified table"""
        prepared_data = self._prepare_data_for_db(data)
        self.logger.debug(f"@@ create_record entry  \n data: {data} \n prepared_data: {prepared_data}")
        columns = list(prepared_data.keys())
        values = list(prepared_data.values())

        placeholders = ",".join(["%s"] * len(columns))
        columns_str = ",".join(columns)

        query = f"""
            INSERT INTO {table_name} ({columns_str})
            VALUES ({placeholders})
            RETURNING {id_field};
        """
        self.logger.info(f"@@ create_record: q={query} params={values}")

        result = self.db_manager.execute_query(
            query=query,
            params=values,
            fetch_one=True
        )

        return result[0] if result else None

    def notify_signaling(self, op, id, u):
        self.sig_client.push_event(event_type = op.value,
                                    payload = {"id": str(id), "uuid": str(u)},
                                    source=self.agent_id,
                                    tags=["orb"]
                                    )

    def get_record(self, table_name: str, record_id: int, columns:list = [], id_field="id") -> Optional[Dict[str, Any]]:
        """Get a record by ID from the specified table"""
        columns_str = "*"
        if columns is None:
            columns = []

            if not columns:
                columns_str = "*"
            else:
                columns_str = ", ".join([f'"{col}"' for col in columns])


        query = f"""
            SELECT {columns_str} FROM {table_name}
            WHERE {id_field} = %s
        """
        self.logger.debug(f"@@ get_record q {query} r: {record_id}")
        result = self.db_manager.execute_query(
            query=query,
            params=(record_id,),
            fetch_one=True
        )
        self.logger.debug(f"@@ get_record result {result}")
        if result:
            if not columns:
                # Get column names directly from table structure
                columns = self._get_table_columns(table_name)

            if columns:
                self.logger.debug(f"@@ get_record has columns {columns}")
                return dict(zip(columns, result))
            else:
                self.logger.debug(f"@@ get_record no columns {columns}")
        return None

    def _get_table_columns(self, table_name: str) -> List[str]:
        """Get column names for a table"""
        query = """
            SELECT column_name
            FROM information_schema.columns
            WHERE table_name = %s
            ORDER BY ordinal_position
        """

        result = self.db_manager.execute_query(
            query=query,
            params=(table_name,),
            fetch_one=False,
            fetch_all=True
        )

        if result:
            return [row[0] for row in result]

        return []

    def update_record(self, table_name: str, record_id, data: Dict[str, Any], id_field="id") -> bool:
        """Update a record in the specified table"""
        prepared_data = self._prepare_data_for_db(data)
        self.logger.debug(f"@@ update_record entry  \n data: {data} \n prepared_data: {prepared_data}")
        columns = list(prepared_data.keys())
        set_clause = ",".join([f"{key} = %s" for key in columns])
        values = list(prepared_data.values()) + [str(record_id)]

        query = f"""
            UPDATE {table_name}
            SET {set_clause}
            WHERE {id_field} = %s
        """
        self.logger.info(f"@@ update_record: q={query} params={values}")
        try:
            self.db_manager.execute_query(
                query=query,
                params=values,
                fetch_one=False
            )

            return True
        except Exception as e:
            self.logger.error(f"Failed to update record: {e}")
            return False

    def delete_record(self, table_name: str, record_id) -> bool:
        """Delete a record from the specified table"""
        query = f"""
            DELETE FROM {table_name}
            WHERE id = %s
        """

        try:
            self.db_manager.execute_query(
                query=query,
                params=(str(record_id),),
                fetch_one=False
            )
            return True
        except Exception as e:
            self.logger.error(f"Failed to delete record: {e}")
            return False

    def find_records(self, table_name: str, conditions: Dict[str, Any] = None,
                     limit: int = 0, offset: int = 0, columns:list = []) -> List[Dict[str, Any]]:
        """Find records in the specified table with optional conditions"""
        query = f"SELECT * FROM {table_name}"
        params = []

        if conditions:
            where_conditions = []
            for key, value in conditions.items():
                where_conditions.append(f"{key} = %s")
                params.append(value)

            query += " WHERE " + " AND ".join(where_conditions)

        query += " ORDER BY id DESC"

        if limit > 0:
            query += f" LIMIT {limit}"
            if offset > 0:
                query += f" OFFSET {offset}"

        # Get column names for the table
        if not columns:
            columns = self._get_table_columns(table_name)

        result = self.db_manager.execute_query(
            query=query,
            params=params,
            fetch_one=False,
            fetch_all=True
        )

        # Convert results to list of dictionaries
        if result and columns:
            return [dict(zip(columns, row)) for row in result]

        return []

    def get_table_info(self, table_name: str) -> Dict[str, Any]:
        """Get information about a table structure"""
        query = """
            SELECT column_name, data_type, is_nullable, column_default
            FROM information_schema.columns
            WHERE table_name = %s
            ORDER BY ordinal_position
        """

        result = self.db_manager.execute_query(
            query=query,
            params=(table_name,),
            fetch_one=False,
            fetch_all=True
        )

        if result:
            columns = ['column_name', 'data_type', 'is_nullable', 'column_default']
            return [dict(zip(columns, row)) for row in result]

        return []

    def execute_raw_query(self, query: str, params: tuple = None) -> List[Dict[str, Any]]:
        """Execute a raw SQL query and return results"""
        # For raw queries, we need to determine column names
        # This is a simplified approach - in practice, you might want to
        # use a more sophisticated approach to get column names

        result = self.db_manager.execute_query(
            query=query,
            params=params,
            fetch_one=False,
            fetch_all=True
        )

        # Since we don't know column names for raw queries, we return as-is
        # or you could implement a more complex column detection logic
        if result:
            # For raw queries, we can't reliably determine column names
            # This would require parsing the query or using a different approach
            pass

        return []

    def push_data(self, data_obj: OrbDataObject) -> OrbDataObject:
        """
        Push OrbDataObject to the database.
        If record doesn't exist, creates new one. If exists, updates it.

        Args:
            data_obj (OrbDataObject): OrbDataObject instance to push to database

        Returns:
            OrbDataObject: Updated object with database ID if new record was created

        Raises:
            StorageError: If database operation fails
            BrokenStorageError: If database connection is not available
            ValueError: If provided object is not an instance
        """
        if not self.db_enabled:
            self.logger.error("Database is not enabled, cannot push data object")
            raise BrokenStorageError("Database connection is not available")
        if not isinstance(data_obj, OrbDataObject):
            raise ValueError("Expected OrbDataObject instance")
        try:
            

            # Check if this is a new record (id is None, <= 0, or doesn't exist)
            is_new_record = (not hasattr(data_obj, 'u') or data_obj.u is None or data_obj.u == uuid.uuid7())
            if is_new_record:
                # make new one
                data_obj.u = uuid.uuid7()
                data_obj.ctime = datetime.now(timezone.utc).replace(tzinfo=None)

            # Prepare data for database operation
            prepared_data = data_obj.to_record()
            if is_new_record:
                new_id = self.create_record('public.orb_data', prepared_data, id_field="u")
                data_obj.u = new_id
                self.notify_signaling(lsig.SignalingEventType.FileOp_new, data_obj.u, data_obj.u)
                self.logger.debug(f"Created new orb_data record with UUID: {data_obj.u}")

            else:
                # Update existing record
                self.logger.info(f"Updating existing orb_data record with UUID: {data_obj.u}")
                success = self.update_record('public.orb_data', data_obj.u, prepared_data, id_field='u')
                if not success:
                    raise StorageError(f"Failed to update orb_data record with ID: {data_obj.u}")
                self.notify_signaling(lsig.SignalingEventType.FileOp_update, data_obj.u, data_obj.u)
                self.logger.debug(f"Successfully updated orb_data record with UUID: {data_obj.u}")

            return data_obj

        except StorageError as e:
            # Re-raise StorageError
            raise
        except Exception as e:
            self.logger.error(f"Unexpected error during push_data operation: {e}")
            raise StorageError(f"Failed to push data object: {e}")

    def push_meta(self, meta_obj: OrbMetaObject) -> OrbMetaObject:
        """
        Push OrbMetaObject to the database.
        If record doesn't exist, creates new one. If exists, updates it.

        Args:
            meta_obj: OrbMetaObject instance to push to database

        Returns:
            OrbMetaObject: Updated object with database ID if new record was created

        Raises:
            StorageError: If database operation fails
            BrokenStorageError: If database connection is not available
        """
        if not self.db_enabled:
            self.logger.error("Database is not enabled, cannot push meta object")
            raise BrokenStorageError("Database connection is not available")

        try:
            # Prepare data for database operation
            data = {
                'data_type': getattr(meta_obj, 'data_type', '@json'),
                'flags': getattr(meta_obj, 'flags', []),
                'src': getattr(meta_obj, 'handle', 0),
                'u': getattr(meta_obj, 'u', uuid.uuid7()),
                'ctime': utime_s()
            }

            # Check if this is a new record (id is None, <= 0, or doesn't exist)
            is_new_record = (not hasattr(meta_obj, 'id') or
                           meta_obj.id is None or
                           meta_obj.id <= 0)

            if is_new_record:
                data['ctime'] = datetime.now(timezone.utc).replace(tzinfo=None)
                self.logger.info(f"Creating new orb_meta record data: {data}")
                new_id = self.create_record('public.orb_meta', data)
                if new_id is None:
                    raise StorageError("Failed to create new orb_meta record")

                # Update the object with the new ID
                meta_obj.id = new_id
                self.notify_signaling(lsig.SignalingEventType.FileOp_new, meta_obj.id, data['u'])
                self.logger.debug(f"Created new orb_meta record with ID: {new_id}")

            else:
                # Update existing record
                self.logger.info(f"Updating existing orb_meta record with ID: {meta_obj.id}")

                # For updates, we don't change the ctime
                success = self.update_record('public.orb_meta', meta_obj.id, data)
                if not success:
                    raise StorageError(f"Failed to update orb_meta record with ID: {meta_obj.id}")

                self.notify_signaling(lsig.SignalingEventType.FileOp_update, meta_obj.id, data['u'])
                self.logger.debug(f"Successfully updated orb_meta record with ID: {meta_obj.id}")

            return meta_obj

        except StorageError:
            # Re-raise StorageError
            raise
        except Exception as e:
            self.logger.error(f"Unexpected error during push_meta operation: {e}")
            raise StorageError(f"Failed to push meta object: {e}")