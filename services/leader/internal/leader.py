from .discover_pg import DiscoverManagerPG
from lunaricorn.utils.db_manager import DatabaseManager
from typing import Optional
import yaml
import logging
import os

class NotReadyException(Exception):
    pass

class Leader:
    CONFIG_PATH = "/opt/lunaricorn/leader_data/leader_config.yaml"
    CLUSTER_CONFIG_PATH = "/opt/lunaricorn/leader_data/cluster_config.yaml"
    def __init__(self) -> None:
        
        self.config = self._load_config()
        self.logger = logging.getLogger(__name__)

        # Read config file values first
        db_type = self.config.get("discover", {}).get("db_type", "postgresql")
        db_host = self.config.get("discover", {}).get("db_host", "localhost")
        db_port = self.config.get("discover", {}).get("db_port", 5432)
        db_user = self.config.get("discover", {}).get("db_user", "postgres")
        db_password = self.config.get("discover", {}).get("db_password", "postgres")
        db_dbname = self.config.get("discover", {}).get("dbname", "lunaricorn")

        # Environment variables override config file values only if they are set
        if "db_type" in os.environ:
            db_type = os.environ["db_type"]
        if "db_host" in os.environ:
            db_host = os.environ["db_host"]
        if "db_port" in os.environ:
            db_port = int(os.environ["db_port"])
        if "db_user" in os.environ:
            db_user = os.environ["db_user"]
        if "db_password" in os.environ:
            db_password = os.environ["db_password"]
        if "db_name" in os.environ:
            db_dbname = os.environ["db_name"]

        # Initialize discover manager (now uses global database manager)
        self.discover_manager = DiscoverManagerPG(
            host=db_host,
            port=db_port,
            user=db_user,
            password=db_password,
            dbname=db_dbname,
            minconn=1,
            maxconn=3
        )
        self.logger.info("Leader initialized with global database manager")
    def shutdown(self):
        if self.discover_manager:
            self.discover_manager.shutdown()

    def _load_config(self) -> dict:
        """Load configuration from YAML file."""
        try:
            with open(self.CONFIG_PATH, "r") as f:
                return yaml.safe_load(f)
        except Exception as e:
            self.logger.error(f"Error loading configuration: {e}")
            raise e
    
    def get_cluster_config(self) -> dict:
        """Load cluster configuration from YAML file."""
        try:
            with open(self.CLUSTER_CONFIG_PATH, "r") as f:
                return yaml.safe_load(f)
        except Exception as e:
            self.logger.error(f"Error loading cluster configuration: {e}")
            raise e
    
    def _get_alive(self):
        """Get alive nodes from database."""
        offet = self.config.get("discover", {}).get("alive_timeout", 10)
        return self.discover_manager.list(offet)
    
    def get_active_nodes(self):
        """Get alive nodes from database."""
        offet = self.config.get("discover", {}).get("alive_timeout", 10)
        return self.discover_manager.list(offet)
    
    def _get_required_nodes(self):
        """Get required nodes from configuration."""
        return self.config.get("discover", {}).get("required_nodes", [])
    
    def get_core_nodes(self):
        alive_nodes = self.get_active_nodes()
        required_nodes = self._get_required_nodes()
        alive_node_names = {node['name'] for node in alive_nodes}
        return [node for node in required_nodes if node in alive_node_names]

    def ready(self):
        """Check if leader is ready to start."""
        alive_nodes = self.get_active_nodes()
        required_nodes = self._get_required_nodes()
        
        # Get names of alive nodes
        alive_node_names = {node['name'] for node in alive_nodes}
        
        # Check if all required nodes are present in alive nodes
        missing_nodes = []
        for required_node in required_nodes:
            if required_node not in alive_node_names:
                missing_nodes.append(required_node)
        
        if missing_nodes:
            self.logger.warning(f"Missing required nodes: {missing_nodes}. Required: {required_nodes}, Alive: {list(alive_node_names)}")
            return False
        
        self.logger.info(f"All required nodes are alive. Required: {required_nodes}, Alive: {list(alive_node_names)}")
        return True
    
    def update_node(self, node_name: str, node_type: str, instance_key: str, host: Optional[str] = None, port: Optional[int] = 0):
        """Update node information in database."""
        return self.discover_manager.update(node_name, node_type, instance_key, host, port)
    
    def get_list(self):
        if not self.ready():
            raise NotReadyException("Leader is not ready to start")
        return self._get_alive()

    def detailed_status(self):
        alive_nodes = self.get_active_nodes()
        required_nodes = self._get_required_nodes()
        
        # Get names of alive nodes
        alive_node_names = {node['name'] for node in alive_nodes}
        nodes_summary = {}
        for node_name in required_nodes:
            nodes_summary[node_name] = "off"
        for node_name in alive_node_names:
            nodes_summary[node_name] = "on"
        return {
            "nodes_summary": nodes_summary,
            "required_nodes": list(set(required_nodes))
        }

    def get_next_message_id(self):
        current_mid = self.discover_manager.get_message_id()
        current_mid += 1
        self.discover_manager.update_message_id(current_mid)
        return current_mid

    def get_next_object_id(self):
        current_oid = self.discover_manager.get_object_id()
        current_oid += 1
        self.discover_manager.update_object_id(current_oid)
        return current_oid

    def update_node_state(self, node: str, ok: bool, msg: str = "ok", ex={}):
        """Update node state in the database."""
        return self.discover_manager.update_node_state(node, ok, msg, ex)

    def get_node_states(self):
        """Get node states from the discover manager."""
        return self.discover_manager.node_states()