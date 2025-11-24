import yaml
import os
import atexit
import sys
import logging
import threading
import time
from flask import Flask

sys.path.append(os.path.dirname(__file__))
sys.path.append(os.getcwd())

from node_config import *
from logger_config import setup_orb_logging
import internal

# Import the Flask app from rest_app
from rest_app import create_app

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
            sys.exit(1)

        logger.info("Starting api server")
        db_config = config.create_db_config()
        storage = internal.DataStorage(db_config)
        if not storage.good():
            logger.error("cannot make storage")
            sys.exit(1)

        # Create and run Flask app
        app = create_app(storage)
        
        tester = internal.StorageTester(storage)
        test_rc = tester.run_all_tests()
        if not test_rc:
            logger.error("❌ self test failed!")
            sys.exit(1)
        logger.info("✅ self test ok")
        # Run Flask app in a separate thread
        def run_flask():
            app.run(host='0.0.0.0', port=8080, debug=True, use_reloader=False)
        
        flask_thread = threading.Thread(target=run_flask)
        flask_thread.daemon = True
        flask_thread.start()
        
        # Keep main thread alive
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            logger.info("Received keyboard interrupt, shutting down...")
            
    except KeyboardInterrupt:
        logger.info("Received keyboard interrupt, shutting down..")
    except KeyError as ke:
        logger.error(f"Configuration error: {ke}")
    except Exception as e:
        logger.error(f"Unexpected error: {e}")
        raise