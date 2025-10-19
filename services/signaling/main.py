import yaml
import os
import atexit
import sys
import logging
import threading
import time
sys.path.append(os.path.dirname(__file__))
sys.path.append(os.getcwd())

from logger_config import setup_signaling_logging
from zmq_server import ZeroMQSignalingServer
from api_server import *

import lunaricorn.api.leader as leader

class NodeController:
    instance = None
    def __init__(self, leader_url):
        self.leader_available = False
        self.connector = None
        self.leader_url = leader_url
        self._abort_event = threading.Event()
        self.node_key = "signaling_main"
        self.node_type = "signaling"
        self.node_name = "signaling"

    @staticmethod
    def abort_registration():
        if NodeController.instance:
            NodeController.instance._abort_event.set()

    def register_node(self):
        logger.info(f"Attempting to connect to leader at: {self.leader_url}")
        self.leader_available = leader.ConnectorUtils.test_connection(self.leader_url)

        if not self.leader_available:
            logger.info(f"Leader API is not available at {self.leader_url}. wait")
            self.connector = None
            rc = False
            start = time.time()
            while not rc and not self._abort_event.is_set():
                logger.info(f"wait for leader ready... {time.time() - start}")
                rc = leader.ConnectorUtils.wait_connection(self.leader_url, 5.0)
            if self._abort_event.is_set():
                self._abort_event.clear()
                return False
        if self.leader_available:
            logger.info(f"Successfully connected to leader API at {self.leader_url}")
            self.connector = leader.ConnectorUtils.create_leader_connector(self.leader_url)
            self.connector.register_service(self.node_name, self.node_key, self.node_type)
            return True
        return False

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
                "rep_port": 5555,
                "pub_port": 5556,
                "api_port": 5557,
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
            },
            "CLUSTER_LEADER_URL": "http://127.0.0.1:8080"
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
    NodeController.abort_registration()
    ServerApp.stop_api_server()

if __name__ == "__main__":
    logger = setup_signaling_logging("signaling_main")
    logger.info("Starting Signaling Service")

    atexit.register(shutdown_handler)
    try:
        # Load configuration
        config = load_config()
        signaling_config = config.get("signaling", {})
        cluster_url = config.get("CLUSTER_LEADER_URL", "")
        if "CLUSTER_LEADER_URL" in os.environ:
            cluster_url = os.environ["CLUSTER_LEADER_URL"]
            logger.info(f"apply env. cluster_url { cluster_url }")

        if not cluster_url:
            logger.error("cluster url not found")
            exit(1)

        logger.info(f"Configuration loaded: {signaling_config}")

        logger.info("Setup Signaling cluster node")
        NodeController.instance = NodeController(cluster_url)
        rc = NodeController.instance.register_node()
        if not rc:
            logger.error("Setup Signaling cluster node - FAILED")
            exit(1)
        logger.info("Setup Signaling cluster node - OK")

        logger.info("Starting http api server")

        # Create and start ZeroMQ signaling server
        # Pass the full config to the server
        server = ZeroMQSignalingServer(config)
        controller = server.signaling
        ServerApp.start_api_server(signaling_config, controller)
        logger.info("Starting ZeroMQ signaling server...")

        server.start()
        ServerApp.stop_api_server()
    except KeyboardInterrupt:
        logger.info("Received keyboard interrupt, shutting down...")
    except Exception as e:
        logger.error(f"Unexpected error: {e}")
        raise
