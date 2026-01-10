import grpc
from concurrent import futures
import logging
from typing import Optional
import uuid
import json
from datetime import datetime
from lunaricorn.api.orb import datastorage_pb2_grpc
from lunaricorn.api.orb.datastorage_pb2 import *
from lunaricorn.types.orb_data_object import OrbDataObject, OrbDataSybtypes
from lunaricorn.types.orb_meta_object import OrbMetaObject

logger = logging.getLogger(__name__)
logger = logging.getLogger(__name__)

class OrbDataService(datastorage_pb2_grpc.OrbDataServiceServicer):
    """gRPC сервер для OrbDataService операций с поддержкой Orb объектов."""
    
    def __init__(self, data_storage):
        """
        Initialize the service.
        
        Args:
            data_storage: Instance of DataStorage with methods:
                - push_data(orb_data_object: OrbDataObject) -> OrbDataObject
                - push_meta(orb_meta_object: OrbMetaObject) -> OrbMetaObject  
                - fetch_meta(id: int) -> Optional[OrbMetaObject]
                - fetch_data(u: str) -> Optional[OrbDataObject]
        """
        self.data_storage = data_storage
        logger.info("OrbDataService initialized")
    
    def _proto_to_orb_data_object(self, proto_obj: OrbDataObject) -> OrbDataObject:
        """Convert protobuf OrbDataObject to internal OrbDataObject"""
        # Parse UUID fields
        u = uuid.UUID(proto_obj.u) if proto_obj.u else None
        chain_left = uuid.UUID(proto_obj.chain_left) if proto_obj.chain_left else None
        chain_right = uuid.UUID(proto_obj.chain_right) if proto_obj.chain_right else None
        parent = uuid.UUID(proto_obj.parent) if proto_obj.parent else None
        
        # Parse ctime
        ctime = None
        if proto_obj.ctime:
            try:
                ctime = datetime.fromisoformat(proto_obj.ctime.replace('Z', '+00:00'))
            except:
                ctime = datetime.utcnow()
        
        # Parse data based on subtype
        data = None
        if proto_obj.data:
            try:
                if proto_obj.subtype == '@json':
                    data = json.loads(proto_obj.data.decode('utf-8'))
                elif proto_obj.subtype == '@raw':
                    data = proto_obj.data
                else:
                    # Default to raw
                    data = proto_obj.data
            except Exception as e:
                logger.warning(f"Failed to parse data for subtype {proto_obj.subtype}: {e}")
                data = proto_obj.data
        
        return OrbDataObject(
            u=u,
            type="@OrbData",
            src=proto_obj.src or None,
            data=data,
            chain_left=chain_left,
            chain_right=chain_right,
            parent=parent,
            ctime=ctime,
            flags=list(proto_obj.flags),
            subtype=proto_obj.subtype or '@json'
        )
    
    def _orb_data_object_to_proto(self, internal_obj: OrbDataObject) -> OrbDataObject:
        """Convert internal OrbDataObject to protobuf OrbDataObject"""
        # Prepare data based on subtype
        data_bytes = b''
        if internal_obj.data:
            try:
                if internal_obj.subtype == '@json':
                    data_bytes = json.dumps(internal_obj.data).encode('utf-8')
                elif internal_obj.subtype == '@raw' and isinstance(internal_obj.data, bytes):
                    data_bytes = internal_obj.data
                elif isinstance(internal_obj.data, bytes):
                    data_bytes = internal_obj.data
                else:
                    # Fallback: try to serialize as JSON
                    data_bytes = json.dumps(internal_obj.data).encode('utf-8')
            except Exception as e:
                logger.warning(f"Failed to serialize data for protobuf: {e}")
                data_bytes = b''
        
        return OrbDataObject(
            u=str(internal_obj.u) if internal_obj.u else "",
            subtype=internal_obj.subtype or '@json',
            chain_left=str(internal_obj.chain_left) if internal_obj.chain_left else "",
            chain_right=str(internal_obj.chain_right) if internal_obj.chain_right else "",
            parent=str(internal_obj.parent) if internal_obj.parent else "",
            ctime=internal_obj.ctime.isoformat() if internal_obj.ctime else "",
            flags=internal_obj.flags or [],
            src=internal_obj.src or "",
            data=data_bytes
        )
    
    def _proto_to_orb_meta_object(self, proto_obj: OrbMetaObject) -> OrbMetaObject:
        """Convert protobuf OrbMetaObject to internal OrbMetaObject"""
        # Parse UUID
        u = uuid.UUID(proto_obj.u) if proto_obj.u else None
        
        # Parse ctime
        ctime = None
        if proto_obj.ctime:
            try:
                ctime = datetime.fromisoformat(proto_obj.ctime.replace('Z', '+00:00'))
            except:
                ctime = datetime.utcnow()
        
        return OrbMetaObject(
            id=proto_obj.id or 0,
            u=u,
            type="@OrbMeta",
            handle=proto_obj.handle or None,
            ctime=ctime,
            flags=list(proto_obj.flags)
        )
    
    def _orb_meta_object_to_proto(self, internal_obj: OrbMetaObject) -> OrbMetaObject:
        """Convert internal OrbMetaObject to protobuf OrbMetaObject"""
        return OrbMetaObject(
            id=internal_obj.id or 0,
            u=str(internal_obj.u) if internal_obj.u else "",
            ctime=internal_obj.ctime.isoformat() if internal_obj.ctime else "",
            type=internal_obj.type or "@OrbMeta",
            handle=internal_obj.handle or 0,
            flags=internal_obj.flags or []
        )
    
    def PushData(self, request, context):
        """Implement gRPC PushData method for backward compatibility."""
        try:
            # Convert to OrbDataObject for internal processing
            internal_obj = OrbDataObject(
                u=uuid.uuid4(),
                type="@OrbData",
                src=request.key or None,
                data={'data': request.data.decode('utf-8')} if request.data else {},
                subtype='@json',
                ctime=datetime.utcnow()
            )
            
            result_obj = self.data_storage.push_data(internal_obj)
            success = result_obj is not None
            
            return PushResponse(success=success, message="Success" if success else "Failed")
        except Exception as e:
            logger.error(f"PushData error: {e}")
            context.set_code(grpc.StatusCode.INTERNAL)
            context.set_details(str(e))
            return PushResponse(success=False, message=str(e))
    
    def PushMeta(self, request, context):
        """Implement gRPC PushMeta method for backward compatibility."""
        try:
            # Convert to OrbMetaObject for internal processing
            internal_obj = OrbMetaObject(
                id=0,
                u=uuid.uuid4(),
                type="@OrbMeta",
                handle=0,
                ctime=datetime.utcnow(),
                flags=[]
            )
            
            result_obj = self.data_storage.push_meta(internal_obj)
            success = result_obj is not None
            
            return PushResponse(success=success, message="Success" if success else "Failed")
        except Exception as e:
            logger.error(f"PushMeta error: {e}")
            context.set_code(grpc.StatusCode.INTERNAL)
            context.set_details(str(e))
            return PushResponse(success=False, message=str(e))
    
    def FetchData(self, request, context):
        """Implement gRPC FetchData method for backward compatibility."""
        try:
            # For backward compatibility, we need to fetch by UUID
            # Assuming key is a UUID string
            data_obj = self.data_storage.fetch_data(request.key)
            if data_obj is None:
                context.set_code(grpc.StatusCode.NOT_FOUND)
                context.set_details(f"Data for key '{request.key}' not found")
                return FetchDataResponse()
            
            # Convert data to bytes
            data_bytes = b''
            if data_obj.data:
                if isinstance(data_obj.data, dict):
                    data_bytes = json.dumps(data_obj.data).encode('utf-8')
                elif isinstance(data_obj.data, bytes):
                    data_bytes = data_obj.data
                else:
                    data_bytes = str(data_obj.data).encode('utf-8')
            
            return FetchDataResponse(data=data_bytes)
        except Exception as e:
            logger.error(f"FetchData error: {e}")
            context.set_code(grpc.StatusCode.INTERNAL)
            context.set_details(str(e))
            return FetchDataResponse()
    
    def FetchMeta(self, request, context):
        """Implement gRPC FetchMeta method for backward compatibility."""
        try:
            # For backward compatibility, we need to convert key to integer ID
            try:
                meta_id = int(request.key)
            except ValueError:
                context.set_code(grpc.StatusCode.INVALID_ARGUMENT)
                context.set_details(f"Invalid meta key '{request.key}', expected integer")
                return FetchMetaResponse()
            
            meta_obj = self.data_storage.fetch_meta(meta_id)
            if meta_obj is None:
                context.set_code(grpc.StatusCode.NOT_FOUND)
                context.set_details(f"Meta for key '{request.key}' not found")
                return FetchMetaResponse()
            
            # Convert meta to bytes (JSON)
            meta_dict = {
                'id': meta_obj.id,
                'u': str(meta_obj.u),
                'type': meta_obj.type,
                'handle': meta_obj.handle,
                'ctime': meta_obj.ctime.isoformat() if meta_obj.ctime else '',
                'flags': meta_obj.flags
            }
            meta_bytes = json.dumps(meta_dict).encode('utf-8')
            
            return FetchMetaResponse(meta=meta_bytes)
        except Exception as e:
            logger.error(f"FetchMeta error: {e}")
            context.set_code(grpc.StatusCode.INTERNAL)
            context.set_details(str(e))
            return FetchMetaResponse()
    
    def PushOrbData(self, request, context):
        """Push OrbDataObject to storage."""
        try:
            # Convert protobuf to internal object
            internal_obj = self._proto_to_orb_data_object(request.data)
            
            # Push to storage
            result_obj = self.data_storage.push_data(internal_obj)
            
            if result_obj is None:
                context.set_code(grpc.StatusCode.INTERNAL)
                context.set_details("Failed to push OrbDataObject")
                return OrbResponse(success=False, message="Push failed", identifier="")
            
            identifier = str(result_obj.u) if result_obj.u else ""
            return OrbResponse(
                success=True, 
                message="Success", 
                identifier=identifier
            )
        except Exception as e:
            logger.error(f"PushOrbData error: {e}")
            context.set_code(grpc.StatusCode.INTERNAL)
            context.set_details(str(e))
            return OrbResponse(success=False, message=str(e), identifier="")
    
    def PushOrbMeta(self, request, context):
        """Push OrbMetaObject to storage."""
        try:
            # Convert protobuf to internal object
            internal_obj = self._proto_to_orb_meta_object(request.meta)
            
            # Push to storage
            result_obj = self.data_storage.push_meta(internal_obj)
            
            if result_obj is None:
                context.set_code(grpc.StatusCode.INTERNAL)
                context.set_details("Failed to push OrbMetaObject")
                return OrbResponse(success=False, message="Push failed", identifier="")
            
            identifier = str(result_obj.id) if result_obj.id else ""
            return OrbResponse(
                success=True, 
                message="Success", 
                identifier=identifier
            )
        except Exception as e:
            logger.error(f"PushOrbMeta error: {e}")
            context.set_code(grpc.StatusCode.INTERNAL)
            context.set_details(str(e))
            return OrbResponse(success=False, message=str(e), identifier="")
    
    def FetchOrbData(self, request, context):
        """Fetch OrbDataObject by identifier."""
        try:
            # Fetch from storage
            data_obj = self.data_storage.fetch_data(request.identifier)
            
            if data_obj is None:
                context.set_code(grpc.StatusCode.NOT_FOUND)
                context.set_details(f"OrbDataObject with identifier '{request.identifier}' not found")
                return OrbDataResponse()
            
            # Convert to protobuf
            proto_obj = self._orb_data_object_to_proto(data_obj)
            
            return OrbDataResponse(data=proto_obj)
        except Exception as e:
            logger.error(f"FetchOrbData error: {e}")
            context.set_code(grpc.StatusCode.INTERNAL)
            context.set_details(str(e))
            return OrbDataResponse()
    
    def FetchOrbMeta(self, request, context):
        """Fetch OrbMetaObject by identifier."""
        try:
            # Try to parse identifier as integer (ID) first
            try:
                meta_id = int(request.identifier)
                meta_obj = self.data_storage.fetch_meta(meta_id)
            except ValueError:
                # If not an integer, try as UUID string
                meta_obj = None
                # Note: fetch_meta expects integer ID, not UUID
                # We need to handle this case differently
                context.set_code(grpc.StatusCode.INVALID_ARGUMENT)
                context.set_details(f"Invalid identifier '{request.identifier}', expected integer ID")
                return OrbMetaResponse()
            
            if meta_obj is None:
                context.set_code(grpc.StatusCode.NOT_FOUND)
                context.set_details(f"OrbMetaObject with identifier '{request.identifier}' not found")
                return OrbMetaResponse()
            
            # Convert to protobuf
            proto_obj = self._orb_meta_object_to_proto(meta_obj)
            
            return OrbMetaResponse(meta=proto_obj)
        except Exception as e:
            logger.error(f"FetchOrbMeta error: {e}")
            context.set_code(grpc.StatusCode.INTERNAL)
            context.set_details(str(e))
            return OrbMetaResponse()


class GRPCServer:
    """Class for managing gRPC server lifecycle."""
    
    def __init__(self, data_storage, host: str = '0.0.0.0', port: int = 50051):
        """
        Initialize gRPC server.
        
        Args:
            data_storage: DataStorage instance
            host: Server bind host
            port: Server bind port
        """
        self.data_storage = data_storage
        self.host = host
        self.port = port
        self.server = None
        self.service = None
        logger.info(f"GRPCServer initialized for {host}:{port}")
    
    def start(self, max_workers: int = 10):
        """Start gRPC server."""
        try:
            # Create server
            self.server = grpc.server(
                futures.ThreadPoolExecutor(max_workers=max_workers)
            )
            
            # Register service
            self.service = OrbDataService(self.data_storage) 
            datastorage_pb2_grpc.add_OrbDataServiceServicer_to_server(
                self.service, self.server
            )
            
            # Add port
            server_address = f'{self.host}:{self.port}'
            self.server.add_insecure_port(server_address)
            
            # Start server
            self.server.start()
            logger.info(f"gRPC server started on {server_address}")
            
            return True
        except Exception as e:
            logger.error(f"Failed to start gRPC server: {e}")
            return False
    
    def stop(self, grace_period: float = 5.0):
        """Stop gRPC server gracefully."""
        if self.server:
            try:
                self.server.stop(grace_period)
                logger.info("gRPC server stopped")
            except Exception as e:
                logger.error(f"Error stopping gRPC server: {e}")
    
    def wait_for_termination(self):
        """Wait for server termination."""
        if self.server:
            self.server.wait_for_termination()
    
    def is_serving(self) -> bool:
        """Check if server is running."""
        if hasattr(self.server, '_state'):
            # For newer grpc versions
            return self.server._state.stage == grpc._server._ServerStage.STARTED
        elif hasattr(self.server, '_serving'):
            # For older versions
            return getattr(self.server, '_serving', False)
        return False


def GRPC_serve(data_storage, host: str = '0.0.0.0', port: int = 50051):
    """
    Convenience function to start server.
    
    Args:
        data_storage: DataStorage instance
        host: Bind host
        port: Bind port
    
    Returns:
        GRPCServer: Instance of running server
    """
    server = GRPCServer(data_storage, host, port)
    if server.start():
        return server
    return None

