from fastapi import APIRouter
import lunaricorn.api.leader as leader
import logging
from datetime import datetime

logger = logging.getLogger(__name__)
router = APIRouter()
cluster_not_ready_since = None

class ClusterEngine:
    config = None
    cluster = None
    counter = 0

    def __init__(self):
        if ClusterEngine.config is None:
            raise ValueError("Config is not loaded")

        self.leader_url = ClusterEngine.config["cluster"]["leader"]
        self.node_key = ClusterEngine.config["portal"]["key"]
        self.node_type = ClusterEngine.config["portal"]["type"]

        logger.info(f"Attempting to connect to leader at: {self.leader_url}")
        self.leader_available = leader.ConnectorUtils.test_connection(self.leader_url)

        if not self.leader_available:
            logger.info(f"Leader API is not available at {self.leader_url}. Portal will operate in standalone mode.")
            self.connector = None
        else:
            logger.info(f"Successfully connected to leader API at {self.leader_url}")
            self.connector = leader.ConnectorUtils.create_leader_connector(self.leader_url)

    def is_ready(self):
        if self.connector is None:
            logger.info("Cluster is not ready - no leader connection available")
            return False
        return self.connector.is_ready()

    def register_node(self):
        if self.connector is None:
            logger.info("Cannot register node - no leader connection available")
            return False
        self.connector.register_service("portal", self.node_key, self.node_type)


@router.get("/ready")
async def ready():
    global cluster_not_ready_since

    if ClusterEngine.cluster is None:
        if cluster_not_ready_since is None:
            cluster_not_ready_since = datetime.now()
        duration = datetime.now() - cluster_not_ready_since
        logger.info(f"Cluster status check: Cluster not initialized (duration: {duration})")
        return {"status": "not_ready", "error": "Cluster not initialized"}

    try:
        ready = ClusterEngine.cluster.is_ready()
        status = "ready" if ready else "not_ready"

        if status == "ready":
            if cluster_not_ready_since is not None:
                duration = datetime.now() - cluster_not_ready_since
                logger.info(f"Cluster status check: {status} (was not ready for: {duration})")
                cluster_not_ready_since = None
            else:
                logger.info(f"Cluster status check: {status}")
        else:
            if cluster_not_ready_since is None:
                cluster_not_ready_since = datetime.now()
            duration = datetime.now() - cluster_not_ready_since
            logger.info(f"Cluster status check: {status} (duration: {duration})")

        return {"status": status}
    except Exception as e:
        if cluster_not_ready_since is None:
            cluster_not_ready_since = datetime.now()
        duration = datetime.now() - cluster_not_ready_since
        logger.info(f"Cluster status check failed: {str(e)} (duration: {duration})")
        return {"status": "error", "error": str(e)}

@router.get("/info")
async def get_cluster_info():
    if ClusterEngine.cluster is None:
        logger.info("Cluster info request: Cluster not initialized")
        return {"status": "error", "error": "Cluster not initialized"}

    try:
        if ClusterEngine.cluster.connector is None:
            logger.info("Cluster info request: No leader connection available")
            return {"status": "error", "error": "No leader connection available"}

        cluster_info = ClusterEngine.cluster.connector.get_cluster_info()
        logger.info("Cluster info request: Successfully retrieved cluster information")
        return {"status": "success", "data": cluster_info}
    except Exception as e:
        logger.error(f"Cluster info request failed: {str(e)}")
        return {"status": "error", "error": str(e)}

@router.get("/nodes")
async def get_nodes():
    if ClusterEngine.cluster is None:
        logger.info("Nodes request: Cluster not initialized")
        return {"status": "error", "error": "Cluster not initialized"}

    try:
        cluster_info = ClusterEngine.cluster.connector.get_cluster_info()
        logger.info("Nodes request: Successfully retrieved nodes information")
        return {"status": "success", "data": cluster_info}
    except Exception as e:
        logger.error(f"Nodes request failed: {str(e)}")
        return {"status": "error", "error": str(e)}
class ClusterEngineNode:
    instance = None

    def make_node(config):
        if not instance:
            instance = ClusterEngineNode(config)

    def __init__(self, config):
        self.config = config
