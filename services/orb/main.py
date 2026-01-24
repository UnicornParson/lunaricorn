import yaml
import os
import atexit
import sys
import logging
import threading
import time
from flask import Flask

sys.path.append(os.path.dirname(__file__))
sys.path.append(os.getcwd())

from node_config import *
from logger_config import setup_orb_logging
import internal
from grpc_app import GRPC_serve, GRPCServer

# Import the Flask app from rest_app
from rest_app import create_app
from lunaricorn.utils.maintenance import *
def start_grpc_server(storage, host: str = '0.0.0.0', port: int = 50051) -> tuple[GRPCServer, threading.Thread]:
    """
    Start GRPC server in a separate thread and return server instance and thread.
    
    Args:
        storage: DataStorage instance for the server
        host: Host for server binding
        port: Port for server binding
    
    Returns:
        Tuple of (GRPCServer instance, thread running the server)
        
    Raises:
        RuntimeError: If failed to start GRPC server
        Exception: Any other startup error
    """
    logger = logging.getLogger(__name__)
    logger.info(f"Starting GRPC server on {host}:{port}")
    
    try:
        grpc_server = GRPC_serve(storage, host=host, port=port)
        
        if not grpc_server:
            raise RuntimeError("GRPC_serve returned None")
        
        if not grpc_server.is_serving():
            raise RuntimeError("GRPC server started but not in serving state")
        
        # Create and start thread for GRPC server
        grpc_thread = threading.Thread(
            target=grpc_server.wait_for_termination,
            name="GRPCServerThread",
            daemon=True
        )
        grpc_thread.start()
        
        logger.info(f"GRPC server started successfully on port {port}")
        return grpc_server, grpc_thread
        
    except Exception as e:
        logger.error(f"Failed to start GRPC server: {e}")
        raise

def stop_grpc_server(grpc_server: GRPCServer, grpc_thread: threading.Thread, timeout: float = 5.0):
    """
    Stop GRPC server gracefully.
    
    Args:
        grpc_server: GRPCServer instance to stop
        grpc_thread: Thread running the GRPC server
        timeout: Timeout for thread join in seconds
    """
    logger = logging.getLogger(__name__)
    
    if not grpc_server:
        logger.warning("No GRPC server instance provided")
        return
    
    logger.info("Stopping GRPC server...")
    try:
        grpc_server.stop()
        
        # Wait for thread to finish
        if grpc_thread and grpc_thread.is_alive():
            grpc_thread.join(timeout=timeout)
            if grpc_thread.is_alive():
                logger.warning(f"GRPC thread did not finish within {timeout} seconds")
            else:
                logger.info("GRPC server stopped successfully")
    except Exception as e:
        logger.error(f"Error stopping GRPC server: {e}")

if __name__ == "__main__":
    logger = setup_orb_logging("orb_main")
    setup_maintenance_logging(owner="orb", token=f"orb_{apptoken()}")
    logger.info("Starting Orb Service")
    try:
        config = internal.load_config()
        logger.info("Setup Orb cluster node")
        NodeController.instance = NodeController(config.CLUSTER_LEADER_URL)
        rc = NodeController.instance.register_node()
        if not rc:
            logger.error("Setup Signaling cluster node - FAILED")
            sys.exit(1)

        logger.info("Starting api server")
        db_config = config.create_db_config()
        storage = internal.DataStorage(config)
        if not storage.good():
            logger.error("cannot make storage")
            sys.exit(1)

        # Create and run Flask app
        app = create_app(storage)
        
        tester = internal.StorageTester(storage)
        test_rc = tester.run_all_tests()
        if not test_rc:
            logger.error("❌ self test failed!")
            sys.exit(1)
        logger.info("✅ self test ok")
        # Run Flask app in a separate thread
        def run_flask():
            app.run(host='0.0.0.0', port=8080, debug=True, use_reloader=False)
        
        flask_thread = threading.Thread(target=run_flask)
        flask_thread.daemon = True
        flask_thread.start()
        
        grpc_host = '0.0.0.0'
        grpc_port = 50051  # Default gRPC port
        grpc_server = None
        grpc_thread = None
        grpc_server, grpc_thread = start_grpc_server(storage)
        
        logger.info("Both Flask and GRPC servers are running")

        # Keep main thread alive
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            logger.info("Received keyboard interrupt, shutting down...")
            
    except KeyboardInterrupt:
        logger.info("Received keyboard interrupt, shutting down..")
    except KeyError as ke:
        logger.error(f"Configuration error: {ke}")
    except Exception as e:
        logger.error(f"Unexpected error: {e}")
        raise
    finally:
        # Stop GRPC server (encapsulated in function)
        stop_grpc_server(grpc_server, grpc_thread)