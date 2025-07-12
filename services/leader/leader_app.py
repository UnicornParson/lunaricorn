from fastapi import FastAPI, HTTPException, Depends, status
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field
from typing import List, Optional, Dict, Any
import uvicorn
from datetime import datetime
import uuid
import os
import shutil
from pathlib import Path
import logging
import logging.handlers
import sys
from internal import *

leader = None

# Create FastAPI app instance
app = FastAPI(
    title="Leader API",
    description="API for service discovery and health monitoring",
    version="1.0.0",
    docs_url="/docs",
    redoc_url="/redoc"
)

# Add CORS middleware
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # Configure this properly for production
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Initialize logging system
def setup_logging():
    """Initialize logging system with file and console handlers"""
    # Create logs directory
    logs_dir = Path("/opt/lunaricorn/leader_data/logs")
    logs_dir.mkdir(parents=True, exist_ok=True)
    
    # Configure root logger
    logger = logging.getLogger()
    logger.setLevel(logging.INFO)
    
    # Clear any existing handlers
    for handler in logger.handlers[:]:
        logger.removeHandler(handler)
    
    # Create formatters
    detailed_formatter = logging.Formatter(
        '%(asctime)s - %(name)s - %(levelname)s - %(filename)s:%(lineno)d - %(funcName)s - %(message)s'
    )
    simple_formatter = logging.Formatter(
        '%(asctime)s - %(levelname)s - %(message)s'
    )
    
    # File handler with rotation (10MB max size, keep 5 backup files)
    file_handler = logging.handlers.RotatingFileHandler(
        logs_dir / "leader_api.log",
        maxBytes=10*1024*1024,  # 10MB
        backupCount=5,
        encoding='utf-8'
    )
    file_handler.setLevel(logging.INFO)
    file_handler.setFormatter(detailed_formatter)
    
    # Console handler
    console_handler = logging.StreamHandler(sys.stdout)
    console_handler.setLevel(logging.INFO)
    console_handler.setFormatter(simple_formatter)
    
    # Add handlers to root logger
    logger.addHandler(file_handler)
    logger.addHandler(console_handler)
    
    # Create specific logger for this application
    app_logger = logging.getLogger("leader_api")
    app_logger.info("Logging system initialized")
    
    return app_logger

# Initialize logging
logger = setup_logging()

# Pydantic models for input data parsing

class ImAliveRequest(BaseModel):
    """Model for /v1/imalive endpoint - service health notification"""
    node_name: str = Field(..., description="Name of the service node")
    node_type: str = Field(..., description="Type of the service node")
    instance_key: str = Field(..., description="Unique instance identifier")
    host: Optional[str] = Field(default=None, description="Host address")
    port: Optional[int] = Field(default=0, description="Port number")
    additional: Optional[Dict[str, Any]] = Field(default=None, description="Additional JSON data")

class DiscoverRequest(BaseModel):
    """Model for /v1/discover endpoint - service discovery query"""
    query: str = Field(..., description="Discovery query string")

# Startup event handler
@app.on_event("startup")
async def startup_event():
    """
    Startup event handler that copies initial data to the data directory
    without overwriting existing files.
    """
    logger.info("Starting Leader API service")
    
    initial_data_path = Path("/opt/lunaricorn/leader/app/initial_data")
    target_data_path = Path("/opt/lunaricorn/leader_data")
    
    # Create target directory if it doesn't exist
    target_data_path.mkdir(parents=True, exist_ok=True)
    logger.info(f"Ensured target data directory exists: {target_data_path}")
    
    # Check if initial data directory exists
    if not initial_data_path.exists():
        logger.warning(f"Initial data directory {initial_data_path} does not exist")
        return
    
    logger.info(f"Checking initial data in {initial_data_path}")
    logger.info(f"Target data directory: {target_data_path}")
    
    # Copy files and directories recursively
    copied_count = 0
    skipped_count = 0
    
    for item in initial_data_path.rglob("*"):
        if item.is_file():
            # Calculate relative path from initial_data directory
            relative_path = item.relative_to(initial_data_path)
            target_file = target_data_path / relative_path
            
            # Create parent directories if they don't exist
            target_file.parent.mkdir(parents=True, exist_ok=True)
            
            # Copy file only if it doesn't exist in target
            if not target_file.exists():
                try:
                    shutil.copy2(item, target_file)
                    logger.info(f"Copied: {relative_path}")
                    copied_count += 1
                except Exception as e:
                    logger.error(f"Error copying {relative_path}: {e}")
            else:
                logger.debug(f"Skipped (exists): {relative_path}")
                skipped_count += 1
    
    logger.info(f"Startup complete: {copied_count} files copied, {skipped_count} files skipped")
    logger.info("Leader API service started successfully")
    global leader
    leader = Leader()
    logger.info("Leader initialized")

# API Endpoints

@app.post("/v1/imalive", tags=["Health"])
async def im_alive(request: ImAliveRequest):
    """
    Service health notification endpoint.
    Other services use this to notify about their existence.
    """
    logger.info(f"Received im_alive notification from {request.node_name} (type: {request.node_type})")
    if not leader:
        logger.error("Leader is not initialized")
        raise HTTPException(status_code=500, detail="Leader is not initialized")
    
    # Input data parsing is handled by Pydantic model validation
    # The request object contains all validated and parsed data:
    # - request.node_name: str
    # - request.node_type: str  
    # - request.instance_key: str
    # - request.host: Optional[str]
    # - request.port: Optional[int]
    # - request.additional: Optional[Dict[str, Any]]

    # Check node_name
    if not request.node_name or not isinstance(request.node_name, str):
        logger.error("Invalid or missing node_name in im_alive request")
        raise HTTPException(status_code=500, detail="Invalid or missing node_name")

    # Check node_type
    if not request.node_type or not isinstance(request.node_type, str):
        logger.error("Invalid or missing node_type in im_alive request")
        raise HTTPException(status_code=500, detail="Invalid or missing node_type")

    # Check instance_key
    if not request.instance_key or not isinstance(request.instance_key, str):
        logger.error("Invalid or missing instance_key in im_alive request")
        raise HTTPException(status_code=500, detail="Invalid or missing instance_key")

    # Check host (optional)
    if request.host is not None and not isinstance(request.host, str):
        logger.error("Invalid host field in im_alive request: must be a string")
        raise HTTPException(status_code=500, detail="Invalid host field: must be a string")

    # Check port (optional)
    if request.port is not None and not isinstance(request.port, int):
        logger.error("Invalid port field in im_alive request: must be an integer")
        raise HTTPException(status_code=500, detail="Invalid port field: must be an integer")

    # Check additional (if present, must be a dict)
    if request.additional is not None and not isinstance(request.additional, dict):
        logger.error("Invalid additional field in im_alive request: must be a dictionary")
        raise HTTPException(status_code=500, detail="Invalid additional field: must be a dictionary")

    rc = leader.update_node(request.node_name, request.node_type, request.instance_key, request.host, request.port)
    if rc:
        response = {"status": "received"}
    else:
        raise HTTPException(status_code=500, detail="Failed to update node")

    logger.debug(f"Im_alive response: {response}")
    return response

@app.get("/v1/list", tags=["Discovery"])
async def list_services():
    """
    List all registered services.
    """
    logger.info("Received request to list services")
    if not leader:
        logger.error("Leader is not initialized")
        raise HTTPException(status_code=500, detail="Leader is not initialized")
    
    try:
        response = leader.get_list()
    except NotReadyException as e:
        logger.error(f"Leader is not ready to start: {e}")
        raise HTTPException(status_code=500, detail="Leader is not ready to start")
    
    response = {
        "services": response,
        "total_count": len(response),
        "timestamp": datetime.now().isoformat()
    }
    return response

@app.post("/v1/discover", tags=["Discovery"])
async def discover_services(request: DiscoverRequest):
    """
    Discover services based on query.
    """
    logger.info(f"Received discovery request with query: {request.query}")
    
    # Input data parsing is handled by Pydantic model validation
    # The request object contains the validated query string:
    # - request.query: str
    
    # TODO: Implement service discovery logic here
    response = {
        "query": request.query,
        "results": [],
        "total_count": 0,
        "timestamp": datetime.now().isoformat()
    }
    
    logger.debug(f"Discover services response: {response}")
    return response

@app.get("/v1/getenv", tags=["Environment"])
async def get_environment():
    """
    Get environment information.
    """
    logger.info("Received request for environment information")
    if leader is None or not leader.ready():
        logger.error("Leader is not ready to start")
        raise HTTPException(status_code=500, detail="Leader is not ready to start")
    
    try:
        cluster_config = leader.get_cluster_config()
    except Exception as e:
        logger.error(f"Error getting cluster configuration: {e}")
        raise HTTPException(status_code=500, detail="Error getting cluster configuration")
    
    
    response = {
        "cfg": cluster_config,
        "core": "1.0.0",
        "timestamp": datetime.now().isoformat()
    }
    
    logger.debug(f"Get environment response: {response}")
    return response

# Health check endpoint
@app.get("/", tags=["Health"])
async def root():
    """Root endpoint with API information"""
    logger.debug("Root endpoint accessed")
    return {
        "message": "Leader API - Service Discovery and Health Monitoring",
        "version": "1.0.0",
        "docs": "/docs",
        "status": "healthy"
    }

@app.get("/health", tags=["Health"])
async def health_check():
    """Health check endpoint"""
    logger.debug("Health check endpoint accessed")
    return {"status": "healthy", "timestamp": datetime.now().isoformat()}

# Error handlers
@app.exception_handler(404)
async def not_found_handler(request, exc):
    """Custom 404 error handler"""
    logger.warning(f"404 error for path: {request.url.path}")
    return {"detail": "Resource not found"}

@app.exception_handler(500)
async def internal_error_handler(request, exc):
    """Custom 500 error handler"""
    logger.error(f"500 error for path: {request.url.path}, error: {exc}")
    return {"detail": "Internal server error"}

# Run the application
if __name__ == "__main__":
    logger.info("Starting Leader API with uvicorn")
    uvicorn.run(
        "leader_app:app",
        host="0.0.0.0",
        port=8000,
        reload=True,
        log_level="info"
    )
