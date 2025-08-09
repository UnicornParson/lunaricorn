import yaml
import os
import atexit
import sys
import logging
from logger_config import setup_logging
from zmq_server import ZeroMQSignalingServer

def load_config():
    config_path = "cfg/config.yaml"
    if not os.path.exists(config_path):
        # Create default config if not exists
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
                "alive_timeout": 10,
                "required_nodes": []
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
    
    # Shutdown database connection
    try:
        db_manager = DatabaseManager()
        db_manager.shutdown()
        logger.info("Database connection closed")
    except Exception as e:
        logger.error(f"Error closing database connection: {e}")

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
        server = ZeroMQSignalingServer(signaling_config)
        
        logger.info("Starting ZeroMQ signaling server...")
        server.start()
        
    except KeyboardInterrupt:
        logger.info("Received keyboard interrupt, shutting down...")
    except Exception as e:
        logger.error(f"Unexpected error: {e}")
        raise
