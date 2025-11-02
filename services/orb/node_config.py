import logging
import threading
import time

import lunaricorn.api.leader as leader

class NodeController:
    instance = None
    def __init__(self, leader_url):
        self.leader_available = False
        self.connector = None
        self.leader_url = leader_url
        self._abort_event = threading.Event()
        self.node_key = "orb_main"
        self.node_type = "orb"
        self.node_name = "orb"

    @staticmethod
    def abort_registration():
        if NodeController.instance:
            NodeController.instance._abort_event.set()

    def register_node(self):
        logger = logging.getLogger(__name__)
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