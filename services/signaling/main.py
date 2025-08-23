import yaml
import os
import atexit
import sys
import logging


sys.path.append(os.path.dirname(__file__))
sys.path.append(os.getcwd())

from logger_config import setup_logging
from zmq_server import ZeroMQSignalingServer

def load_config():
    config_path = "cfg/config.yaml"

    logger = logging.getLogger(__name__)
    logger.info("Shutting down Signaling Server...")
    if not os.path.exists(config_path):
        logger.info("Create default config ")
        default_config = {
            "signaling": {
                "name": "signaling",
                "host": "0.0.0.0",
                "port": 5555,
                "zmq": {
                    "protocol": "tcp",
                    "bind_address": "*",
                    "max_connections": 100,
                    "heartbeat_interval": 30,
                    "timeout": 60
                }
            },
            "message_storage": {
                "db_type": "postgresql",
                "db_host": "localhost",
                "db_port": 5432,
                "db_user": "postgres",
                "db_password": "postgres",
                "dbname": "lunaricorn",
                "subscriber_timeout": 300,
                "max_events": 1000
            }
        }
        
        # Ensure cfg directory exists
        os.makedirs("cfg", exist_ok=True)
        
        with open(config_path, "w") as f:
            yaml.dump(default_config, f, default_flow_style=False)
        
        return default_config
    
    with open(config_path, "r") as f:
        config = yaml.safe_load(f)
    return config

def shutdown_handler():
    """Handle graceful shutdown"""
    logger = logging.getLogger(__name__)
    logger.info("Shutting down Signaling Server...")

if __name__ == "__main__":
    logger = setup_logging("portal_main")
    logger.info("Starting Signaling Service")
    
    atexit.register(shutdown_handler)
    try:
        # Load configuration
        config = load_config()
        signaling_config = config.get("signaling", {})
        
        logger.info(f"Configuration loaded: {signaling_config}")
        
        # Create and start ZeroMQ signaling server  
        # Pass the full config to the server
        server = ZeroMQSignalingServer(config)
        
        logger.info("Starting ZeroMQ signaling server...")
        server.start()
        
    except KeyboardInterrupt:
        logger.info("Received keyboard interrupt, shutting down...")
    except Exception as e:
        logger.error(f"Unexpected error: {e}")
        raise
