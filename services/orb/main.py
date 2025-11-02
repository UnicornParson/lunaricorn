import yaml
import os
import atexit
import sys
import logging
import threading
import time
import uvicorn
sys.path.append(os.path.dirname(__file__))
sys.path.append(os.getcwd())

from node_config import *
from logger_config import setup_orb_logging

import internal


if __name__ == "__main__":
    logger = setup_orb_logging("orb_main")
    logger.info("Starting Orb Service")
    try:
        config = internal.load_config()
        logger.info("Setup Orb cluster node")
        NodeController.instance = NodeController(config.CLUSTER_LEADER_URL)
        rc = NodeController.instance.register_node()
        if not rc:
            logger.error("Setup Signaling cluster node - FAILED")
            exit(1)

        logger.info("Starting api server")

        uvicorn.run(
            "app:app",
            host="0.0.0.0",
            port=8080,
            reload=True,
            log_level="info"
        )
    except KeyboardInterrupt:
        logger.info("Received keyboard interrupt, shutting down...")
    except KeyError as ke:
        logger.error(f"Configuration error: {ke}")
    except Exception as e:
        logger.error(f"Unexpected error: {e}")
        raise
