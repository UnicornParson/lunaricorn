import grpc
from concurrent import futures
import logging
from typing import Optional
import datastorage_pb2
import datastorage_pb2_grpc

logger = logging.getLogger(__name__)

class DataStorageService(datastorage_pb2_grpc.DataStorageServiceServicer):
    """gRPC server for DataStorage operations."""
    
    def __init__(self, data_storage):
        """
        Initialize the service.
        
        Args:
            data_storage: Instance of DataStorage with methods:
                - push_data(key: str, data: bytes) -> bool
                - push_meta(key: str, meta: bytes) -> bool  
                - fetch_meta(key: str) -> Optional[bytes]
                - fetch_data(key: str) -> Optional[bytes]
        """
        self.data_storage = data_storage
        logger.info("DataStorageService initialized")
    
    def PushData(self, request, context):
        """Implement gRPC PushData method."""
        try:
            success = self.data_storage.push_data(request.key, request.data)
            return datastorage_pb2.PushDataResponse(success=success)
        except Exception as e:
            logger.error(f"PushData error: {e}")
            context.set_code(grpc.StatusCode.INTERNAL)
            context.set_details(str(e))
            return datastorage_pb2.PushDataResponse(success=False)
    
    def PushMeta(self, request, context):
        """Implement gRPC PushMeta method."""
        try:
            success = self.data_storage.push_meta(request.key, request.meta)
            return datastorage_pb2.PushMetaResponse(success=success)
        except Exception as e:
            logger.error(f"PushMeta error: {e}")
            context.set_code(grpc.StatusCode.INTERNAL)
            context.set_details(str(e))
            return datastorage_pb2.PushMetaResponse(success=False)
    
    def FetchMeta(self, request, context):
        """Implement gRPC FetchMeta method."""
        try:
            meta_data = self.data_storage.fetch_meta(request.key)
            if meta_data is None:
                context.set_code(grpc.StatusCode.NOT_FOUND)
                context.set_details(f"Meta for key '{request.key}' not found")
                return datastorage_pb2.FetchMetaResponse()
            return datastorage_pb2.FetchMetaResponse(meta=meta_data)
        except Exception as e:
            logger.error(f"FetchMeta error: {e}")
            context.set_code(grpc.StatusCode.INTERNAL)
            context.set_details(str(e))
            return datastorage_pb2.FetchMetaResponse()
    
    def FetchData(self, request, context):
        """Implement gRPC FetchData method."""
        try:
            data = self.data_storage.fetch_data(request.key)
            if data is None:
                context.set_code(grpc.StatusCode.NOT_FOUND)
                context.set_details(f"Data for key '{request.key}' not found")
                return datastorage_pb2.FetchDataResponse()
            return datastorage_pb2.FetchDataResponse(data=data)
        except Exception as e:
            logger.error(f"FetchData error: {e}")
            context.set_code(grpc.StatusCode.INTERNAL)
            context.set_details(str(e))
            return datastorage_pb2.FetchDataResponse()


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
            self.service = DataStorageService(self.data_storage)
            datastorage_pb2_grpc.add_DataStorageServiceServicer_to_server(
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

