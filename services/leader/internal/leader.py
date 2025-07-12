from .discover import *
import yaml
import logging

class NotReadyException(Exception):
    pass

class Leader:
    CONFIG_PATH = "/opt/lunaricorn/leader_data/leader_config.yaml"
    CLUSTER_CONFIG_PATH = "/opt/lunaricorn/leader_data/cluster_config.yaml"
    def __init__(self) -> None:
        self.discover_manager = DiscoverManager()
        self.config = self._load_config()
        self.logger = logging.getLogger(__name__)
        self.logger.info("Leader initialized")
    
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