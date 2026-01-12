from flask import Flask, jsonify, request, make_response
from werkzeug.exceptions import HTTPException
from datetime import datetime
import logging
import logging.handlers
from pathlib import Path
from internal.leader import Leader, NotReadyException
from lunaricorn.utils.db_manager import DatabaseManager
import atexit
from lunaricorn.utils.logger_config import *
from lunaricorn.utils.maintenance import *

logger = setup_logging("leader_api", "/opt/lunaricorn/leader_data/logs")
setup_maintenance_logging(owner="leader", token=f"leader_{apptoken}")

logger.info("Leader API started")
app = Flask(__name__)

try:
    leader = Leader()
except Exception as e:
    logger.error(f"Failed to initialize Leader: {e}")
    leader = None


@app.route("/v1/imalive", methods=["POST"])
def im_alive():
    data = request.get_json(force=True)
    node_name = data.get("node_name")
    node_type = data.get("node_type")
    instance_key = data.get("instance_key")
    host = data.get("host", None)
    port = data.get("port", 0)
    additional = data.get("additional", None)

    logger.info(f"Received im_alive notification from {node_name} (type: {node_type})")
    if not leader:
        logger.error("Leader is not initialized")
        return make_response(jsonify({"message": "Leader is not initialized"}), 500)

    # Input validation (mimics Pydantic)
    if not node_name or not isinstance(node_name, str):
        logger.error("Invalid or missing node_name in im_alive request")
        return make_response(jsonify({"message": "Invalid or missing node_name"}), 500)
    if not node_type or not isinstance(node_type, str):
        logger.error("Invalid or missing node_type in im_alive request")
        return make_response(jsonify({"message": "Invalid or missing node_type"}), 500)
    if not instance_key or not isinstance(instance_key, str):
        logger.error("Invalid or missing instance_key in im_alive request")
        return make_response(jsonify({"message": "Invalid or missing instance_key"}), 500)
    if host is not None and not isinstance(host, str):
        logger.error("Invalid host field in im_alive request: must be a string")
        return make_response(jsonify({"message": "Invalid host field: must be a string"}), 500)
    if port is not None and not isinstance(port, int):
        logger.error("Invalid port field in im_alive request: must be an integer")
        return make_response(jsonify({"message": "Invalid port field: must be an integer"}), 500)
    if additional is not None and not isinstance(additional, dict):
        logger.error("Invalid additional field in im_alive request: must be a dictionary")
        return make_response(jsonify({"message": "Invalid additional field: must be a dictionary"}), 500)

    rc = leader.update_node(node_name, node_type, instance_key, host, port)
    if rc:
        response = {"status": "received"}
    else:
        return make_response(jsonify({"message": "Failed to update node"}), 500)

    logger.debug(f"Im_alive response: {response}")
    return jsonify(response)

@app.route("/v1/list", methods=["GET"])
def list_services():
    logger.info("Received request to list services")
    if not leader:
        logger.error("Leader is not initialized")
        return make_response(jsonify({"message": "Leader is not initialized"}), 500)
    try:
        services = leader.get_list()
    except NotReadyException as e:
        logger.error(f"Leader is not ready to start: {e}")
        return make_response(jsonify({"message": f"Leader is not ready to start {datetime.now().isoformat()}"}), 500)
    response = {
        "services": services,
        "total_count": len(services),
        "timestamp": datetime.now().isoformat()
    }
    return jsonify(response)

@app.route("/v1/discover", methods=["POST"])
def discover_services():
    data = request.get_json(force=True)
    query = data.get("query", "")
    logger.info(f"Received discovery request with query: {query}")
    # TODO: Implement real discovery logic if needed
    response = {
        "query": query,
        "results": [],
        "total_count": 0,
        "timestamp": datetime.now().isoformat()
    }
    logger.debug(f"Discover services response: {response}")
    return jsonify(response)

@app.route("/v1/test", methods=["GET"])
def test():
    return jsonify({"message": "Hello, World!"})

@app.route("/v1/clusterinfo", methods=["GET"])
def get_cluster_info():
    logger.info("Received request for cluster information")
    if leader is None or not hasattr(leader, 'detailed_status'):
        logger.error("Leader is not initialized or missing required method")
        return make_response(jsonify({"message": "Leader service is initializing"}), 503)
    return jsonify(leader.detailed_status())

@app.route("/v1/getenv", methods=["GET"])
def get_environment():
    logger.info("Received request for environment information")
    if leader is None or not leader.ready():
        logger.error("Leader is not ready to start")
        return make_response(jsonify({"message": "Leader is not ready to start"}), 500)
    try:
        cluster_config = leader.get_cluster_config()
    except Exception as e:
        logger.error(f"Error getting cluster configuration: {e}")
        return make_response(jsonify({"message": "Error getting cluster configuration"}), 500)
    response = {
        "cfg": cluster_config,
        "core": "1.0.0",
        "timestamp": datetime.now().isoformat()
    }
    logger.debug(f"Get environment response: {response}")
    return jsonify(response)

@app.route("/v1/utils/get_mid", methods=["GET"])
def get_mid():
    logger.info("Received request for next message id")
    if leader is None:
        logger.error("Leader is not ready to start")
        return make_response(jsonify({"message": "Leader is not ready to start"}), 500)
    return jsonify({"mid": leader.get_next_message_id()})

@app.route("/v1/utils/get_oid", methods=["GET"])
def get_oid():
    logger.info("Received request for next object id")
    if leader is None:
        logger.error("Leader is not ready to start")
        return make_response(jsonify({"message": "Leader is not ready to start"}), 500)
    return jsonify({"oid": leader.get_next_object_id()})

@app.route("/", methods=["GET"])
def root():
    logger.debug("Root endpoint accessed")
    return jsonify({
        "message": "Leader API - Service Discovery and Health Monitoring",
        "version": "1.0.0",
        "docs": "/docs",
        "status": "healthy"
    })

@app.route("/health", methods=["GET"])
def health_check():
    logger.debug("Health check endpoint accessed")
    return jsonify({"status": "healthy", "timestamp": datetime.now().isoformat()})

@app.route("/v1", methods=["GET"])
def api_root():
    logger.debug("API root endpoint accessed")
    return jsonify({
        "message": "Leader API v1",
        "version": "1.0.0",
        "endpoints": {
            "imalive": "/v1/imalive",
            "list": "/v1/list",
            "discover": "/v1/discover",
            "getenv": "/v1/getenv",
            "clusterinfo": "/v1/clusterinfo",
            "utils": {
                "get_mid": "/v1/utils/get_mid",
                "get_oid": "/v1/utils/get_oid"
            }
        },
        "status": "healthy"
    })

# --- Error handlers ---

@app.errorhandler(404)
def not_found_handler(e):
    return make_response(jsonify({"message": "Endpoint not found"}), 404)

@app.errorhandler(Exception)
def general_exception_handler(e):
    logger.exception("Unhandled exception occurred")
    code = 500
    if isinstance(e, HTTPException):
        code = e.code
    return make_response(jsonify({"message": f"Internal server error: {e}"}), code)


if __name__ == "__main__":
    #wait_for_loki_ready(host="loki", port=3100)
    # Register shutdown handler
    def shutdown_handler():
        logger.info("Shutting down Leader API...")
        if leader:
            leader.shutdown()
        logger.info("Leader API shutdown complete")
    
    atexit.register(shutdown_handler)
    
    app.run(host="0.0.0.0", port=8000, debug=True)
