from flask import Flask, jsonify, request, make_response
from werkzeug.exceptions import HTTPException
from werkzeug.serving import make_server
import threading
from datetime import datetime
import logging
import logging.handlers
from pathlib import Path
from typing import Dict, Any, List
from enum import Enum
from functools import wraps
from collections import defaultdict
from dataclasses import dataclass
from lunaricorn.api.leader import *
from lunaricorn.utils.db_manager import DatabaseManager
from logger_config import setup_signaling_logging
import time as time_module

from internal import *


class SiagnalingApiServer(threading.Thread):
    ENDPOINT_NAME = "SignalingApi"
    def __init__(self, config: Dict[str, Any], signaling_controller):
        super().__init__()
        self.config = config
        self.logger = logging.getLogger(SiagnalingApiServer.ENDPOINT_NAME)
        self.signaling_controller = signaling_controller

        self.host = "0.0.0.0"
        self.port = config.get("api_port", 5557)
        self.app = Flask(SiagnalingApiServer.ENDPOINT_NAME)
        self.app.config['COMPRESS_ALGORITHM'] = ['br', 'gzip']
        self.app.config['COMPRESS_LEVEL'] = 9
        self.app.config['COMPRESS_GZIP_LEVEL'] = 9
        self.app.config['COMPRESS_BR_LEVEL'] = 11
        self.app.config['COMPRESS_MIN_SIZE'] = 100
        self._stop_event = threading.Event()
        self.server = None
        self.setup_routes()
    
    def measure_time(self, f):
        @wraps(f)
        def decorated_function(*args, **kwargs):
            start_time = time_module.perf_counter()
            try:
                result = f(*args, **kwargs)
                return result
            finally:
                end_time = time_module.perf_counter()
                execution_time = end_time - start_time
                print(f"ROBOTS: {request.path} - {execution_time}")
                #metrics.add_record(request.path, execution_time)
        return decorated_function
    
    def setup_list_routes(self):
        @self.app.route('/v1/list/tags', methods=["GET"])
        @self.measure_time
        def list_tags():
            field = "tags"
            try:
                values = self.signaling_controller.u_list(field)
            except Exception as e:
                self.logger.error(f"cannot get ulist for {field} reason: {e}")
                return jsonify({"error": f"Internal Server Error. cannot get ulist for {field}"}), 500
            return jsonify(values)
        
        @self.app.route('/v1/list/types', methods=["GET"])
        @self.measure_time
        def list_types():
            field = "type"
            try:
                values = self.signaling_controller.u_list(field)
            except Exception as e:
                self.logger.error(f"cannot get ulist for {field} reason: {e}")
                return jsonify({"error": f"Internal Server Error. cannot get ulist for {field}"}), 500
            return jsonify(values)
        
        @self.app.route('/v1/list/affected', methods=["GET"])
        @self.measure_time
        def list_affected():
            field = "affected"
            try:
                values = self.signaling_controller.u_list(field)
            except Exception as e:
                self.logger.error(f"cannot get ulist for {field} reason: {e}")
                return jsonify({"error": f"Internal Server Error. cannot get ulist for {field}"}), 500
            return jsonify(values)
        
        @self.app.route('/v1/list/owners', methods=["GET"])
        @self.measure_time
        def list_owners():
            field = "owner"
            try:
                values = self.signaling_controller.u_list(field)
            except Exception as e:
                self.logger.error(f"cannot get ulist for {field} reason: {e}")
                return jsonify({"error": f"Internal Server Error. cannot get ulist for {field}"}), 500
            return jsonify(values)
    def setup_stat_routes(self):
        @self.app.route("/v1/stat/clients", methods=["get"])
        @self.measure_time
        def active_clients_list():
            active_clients = self.signaling_controller.active_clients()
            return jsonify(active_clients)

    def setup_routes(self):
        @self.app.route('/', methods=["GET"])
        @self.measure_time
        def index():
            return "Lunaricorn Signaling Api"
        
        @self.app.route('/health', methods=["GET"])
        @self.measure_time
        def health():
            return jsonify({"status": "ok"})
        self.setup_list_routes()
        self.setup_stat_routes()
        
        @self.app.route("/v1/browse", methods=["POST"])
        @self.measure_time
        def events_list():
            data = request.get_json(force=True)
            request_data = BrowseRequest.from_dict(data)
            events = self.signaling_controller.browse(filter=request_data)
            return jsonify(events)
        


    def run(self):
        self.server = make_server(self.host, self.port, self.app)
        self.ctx = self.app.app_context()
        self.ctx.push()
        self.logger.info(f"Starting server on {self.host}:{self.port}")
        while not self._stop_event.is_set():
            self.server.handle_request()
        self.logger.info(f"Stoppeing server on {self.host}:{self.port}")

    def stop_server(self):
        if self.server:
            self._stop_event.set()
            self.server.shutdown()
            self.logger.info("Server stopped")

class ServerApp:
    server_thread = None

    @staticmethod
    def start_api_server(config: Dict[str, Any], controller):
        if ServerApp.server_thread:
            ServerApp.stop_api_server()
            ServerApp.server_thread = None
        ServerApp.server_thread = SiagnalingApiServer(config, controller)
        ServerApp.server_thread.daemon = False
        ServerApp.server_thread.start()

    @staticmethod
    def stop_api_server():
        print("Stopping server thread...")
        if not ServerApp.server_thread:
            return
        ServerApp.server_thread.stop()
        ServerApp.server_thread.join(timeout=5)
        
        if ServerApp.server_thread.is_alive():
            print("Server thread did not stop gracefully, forcing exit")
        else:
            print("Server thread stopped successfully")
        ServerApp.server_thread = None