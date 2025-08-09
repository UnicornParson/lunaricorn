from fastapi import FastAPI, Request
from fastapi.staticfiles import StaticFiles
from api import cluster
import logging
from fastapi.responses import FileResponse, JSONResponse
from datetime import datetime
import yaml
from api.cluster import ClusterEngine
import asyncio
import threading
import time
import signal
import sys
import os
from lunaricorn.api.leader import ConnectorUtils
from lunaricorn.utils.logger_config import setup_logging

logger = setup_logging("portal_app", "/opt/lunaricorn/portal_data/logs")

# Global shutdown flag for graceful termination
shutdown_event = threading.Event()
retry_thread = None

app = FastAPI()

app.include_router(cluster.router, prefix="/api/cluster")

app.mount("/static", StaticFiles(directory="static", html=True), name="static")

@app.get("/", tags=["Root"])
async def root():
    """Serve the main HTML page"""
    return FileResponse("static/index.html")

@app.get("/favicon.ico", tags=["Root"])
async def favicon():
    return FileResponse("static/favicon.ico")

@app.get("/health", tags=["Health"])
async def health_check():
    """Health check endpoint"""
    logger.debug("Health check endpoint accessed")
    return {"status": "healthy", "timestamp": datetime.now().isoformat()}

# Error handlers
@app.exception_handler(404)
async def not_found_handler(request: Request, exc):
    """Custom 404 error handler"""
    logger.warning(f"404 error for path: {request.url.path}")
    return JSONResponse(
        status_code=404,
        content={"detail": f"Resource not found: {request.url.path}"}
    )

@app.exception_handler(500)
async def internal_error_handler(request: Request, exc):
    """Custom 500 error handler"""
    logger.error(f"500 error for path: {request.url.path}, error: {exc}")
    return JSONResponse(
        status_code=500,
        content={"detail": f"Internal server error: {request.url.path}"}
    )

def load_config():
    with open("cfg/config.yaml", "r") as f:
        config = yaml.safe_load(f)
    
    # Check for environment variable override for cluster leader
    env_leader_url = os.environ.get('CLUSTER_LEADER_URL')
    if env_leader_url:
        logger.info(f"Using environment variable CLUSTER_LEADER_URL: {env_leader_url}")
        if 'cluster' not in config:
            config['cluster'] = {}
        config['cluster']['leader'] = env_leader_url
    else:
        logger.info("Using cluster leader URL from config file")
    
    return config

def init_components(config):
    logger.info(f"init_components started: {config}")
    ClusterEngine.config = config
    try:
        ClusterEngine.cluster = ClusterEngine()
        logger.info("Portal components initialized successfully")
        return True
    except Exception as e:
        logger.info(f"Failed to initialize cluster connection: {e}. Portal will operate in standalone mode.")
        ClusterEngine.cluster = None
        return False

def retry_cluster_connection():
    """Background thread function to retry cluster connection every 2 seconds"""
    global shutdown_event
    while not shutdown_event.is_set():
        try:
            if ClusterEngine.cluster is None:
                logger.info("Retrying cluster connection...")
                ClusterEngine.cluster = ClusterEngine()
                logger.info("Cluster connection established successfully after retry")
                break
            else:
                # Cluster is already connected, no need to retry
                break
        except Exception as e:
            logger.info(f"Cluster connection retry failed: {e}. Will retry in 2 seconds.")
            # Check for shutdown every 0.5 seconds during sleep
            for _ in range(4):  # 2 seconds / 0.5 seconds = 4 checks
                if shutdown_event.is_set():
                    logger.info("Shutdown detected, stopping retry thread")
                    return
                time.sleep(0.5)
    
    if shutdown_event.is_set():
        logger.info("Shutdown detected, stopping retry thread")
    else:
        logger.info("Cluster connection established, retry thread stopping")
    logger.info("retry_cluster_connection finished")

def register_service():
    logger.info("register_service started")
    # Register the portal service with the leader using parameters from config
    portal_cfg = ClusterEngine.config.get("portal", {})
    cluster_cfg = ClusterEngine.config.get("cluster", {})
    leader_url = cluster_cfg.get("leader")

    # Extract registration parameters from config
    node_name = portal_cfg.get("name")
    node_type = portal_cfg.get("type")
    instance_key = portal_cfg.get("key")
    host = portal_cfg.get("host")
    port = portal_cfg.get("port")

    # Create the connector (if not already provided)
    connector = ConnectorUtils.create_leader_connector(base_url=leader_url)

    try:
        # Register the service with the leader
        response = connector.register_service(
            node_name=node_name,
            node_type=node_type,
            instance_key=instance_key,
            host=host,
            port=port
        )
        logger.info(f"Service registered with leader: {response}")
        return response
    except Exception as e:
        logger.error(f"Failed to register service with leader: {e}")
        return None

def signal_handler(signum, frame):
    """Handle shutdown signals gracefully"""
    global shutdown_event, retry_thread
    logger.info(f"Received signal {signum}, initiating graceful shutdown...")
    shutdown_event.set()
    
    # Wait for retry thread to finish (max 5 seconds)
    if retry_thread and retry_thread.is_alive():
        logger.info("Waiting for retry thread to finish...")
        retry_thread.join(timeout=5)
        if retry_thread.is_alive():
            logger.warning("Retry thread did not finish within timeout")
    
    logger.info("Graceful shutdown completed")
    sys.exit(0)

def periodic_register_service():
    """Periodically call register_service every 5 seconds until shutdown_event is set."""
    while not shutdown_event.is_set():
        try:
            register_service()
        except Exception as e:
            logger.error(f"Error during periodic service registration: {e}")
        # Wait for 5 seconds or until shutdown_event is set
        if shutdown_event.wait(5):
            break

# Initialize components when module is imported
logger.info("Starting Portal API with uvicorn")

# Set up signal handlers for graceful shutdown
signal.signal(signal.SIGTERM, signal_handler)
signal.signal(signal.SIGINT, signal_handler)

# Initialize components
config = load_config()
init_success = init_components(config)
logger.info(f"init_components finished: {init_success}")

# If initial initialization failed, start retry thread
if not init_success:
    logger.info("Starting background retry thread for cluster connection")
    retry_thread = threading.Thread(target=retry_cluster_connection, name="RetryClusterConnection", daemon=True)
    retry_thread.start()
    
# Start the periodic registration in a background thread
register_thread = threading.Thread(target=periodic_register_service, name="PeriodicRegisterService", daemon=True)
register_thread.start()

logger.info("Portal API application initialized and ready") 