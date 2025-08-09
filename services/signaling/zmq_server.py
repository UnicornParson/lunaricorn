import zmq
import json
import threading
import time
import logging
import os
import sys
from typing import Dict, Any, Optional
from dataclasses import dataclass
from enum import Enum

# Add lunaricorn to path for imports
sys.path.append(os.path.join(os.path.dirname(__file__), '../../..'))
from lunaricorn.utils.db_manager import DatabaseManager

class MessageType(Enum):
    """Message types for signaling server"""
    REGISTER = "register"
    UNREGISTER = "unregister"
    HEARTBEAT = "heartbeat"
    MESSAGE = "message"
    BROADCAST = "broadcast"
    PEER_DISCOVERY = "peer_discovery"
    PEER_LIST = "peer_list"
    IM_ALIVE = "im_alive"

@dataclass
class ClientInfo:
    """Client information structure"""
    id: str
    address: str
    last_seen: float
    metadata: Dict[str, Any]
    socket_type: str

class ZeroMQSignalingServer:
    """ZeroMQ signaling server for peer-to-peer communication"""
    
    def __init__(self, config: Dict[str, Any]):
        self.config = config
        self.logger = logging.getLogger(__name__)
        
        # ZeroMQ context
        self.context = zmq.Context()
        
        # Server configuration
        self.host = config.get("host", "0.0.0.0")
        self.port = config.get("port", 5555)
        self.protocol = config.get("zmq", {}).get("protocol", "tcp")
        self.bind_address = config.get("zmq", {}).get("bind_address", "*")
        self.max_connections = config.get("zmq", {}).get("max_connections", 100)
        self.heartbeat_interval = config.get("zmq", {}).get("heartbeat_interval", 30)
        self.timeout = config.get("zmq", {}).get("timeout", 60)
        
        # Database configuration
        self._setup_database()
        
        # Client registry
        self.clients: Dict[str, ClientInfo] = {}
        self.clients_lock = threading.Lock()
        
        # Socket types
        self.sockets = {}
        self.running = False
        
        # Initialize sockets
        self._setup_sockets()
    
    def _setup_database(self):
        """Setup database connection with environment variable override"""
        # Read config file values first
        db_type = self.config.get("message_storage", {}).get("db_type", "postgresql")
        db_host = self.config.get("message_storage", {}).get("db_host", "localhost")
        db_port = self.config.get("message_storage", {}).get("db_port", 5432)
        db_user = self.config.get("message_storage", {}).get("db_user", "postgres")
        db_password = self.config.get("message_storage", {}).get("db_password", "postgres")
        db_dbname = self.config.get("message_storage", {}).get("dbname", "lunaricorn")

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

        # Initialize global database manager
        try:
            db_manager = DatabaseManager()
            db_manager.initialize(
                host=db_host,
                port=db_port,
                user=db_user,
                password=db_password,
                dbname=db_dbname,
                minconn=1,
                maxconn=3
            )
            self.db_manager = db_manager
            self.logger.info("Database connection initialized for signaling server")
        except Exception as e:
            self.logger.error(f"Failed to initialize database connection: {e}")
            # Continue without database if connection fails
            self.db_enabled = False
        else:
            self.db_enabled = True
    
    def _setup_sockets(self):
        """Setup different types of ZeroMQ sockets"""
        try:
            # ROUTER socket for handling multiple clients
            self.router_socket = self.context.socket(zmq.ROUTER)
            router_address = f"{self.protocol}://{self.bind_address}:{self.port}"
            self.router_socket.bind(router_address)
            self.logger.info(f"ROUTER socket bound to {router_address}")
            
            # PUB socket for broadcasting messages
            self.pub_socket = self.context.socket(zmq.PUB)
            pub_address = f"{self.protocol}://{self.bind_address}:{self.port + 1}"
            self.pub_socket.bind(pub_address)
            self.logger.info(f"PUB socket bound to {pub_address}")
            
            # PULL socket for receiving messages from clients
            self.pull_socket = self.context.socket(zmq.PULL)
            pull_address = f"{self.protocol}://{self.bind_address}:{self.port + 2}"
            self.pull_socket.bind(pull_address)
            self.logger.info(f"PULL socket bound to {pull_address}")
            
        except Exception as e:
            self.logger.error(f"Failed to setup sockets: {e}")
            raise
    
    def _handle_register(self, client_id: str, address: str, metadata: Dict[str, Any]):
        """Handle client registration"""
        with self.clients_lock:
            if len(self.clients) >= self.max_connections:
                return {"status": "error", "message": "Maximum connections reached"}
            
            client_info = ClientInfo(
                id=client_id,
                address=address,
                last_seen=time.time(),
                metadata=metadata,
                socket_type="router"
            )
            
            self.clients[client_id] = client_info
            self.logger.info(f"Client {client_id} registered from {address}")
            
            # Update database if enabled
            if self.db_enabled:
                try:
                    self._update_node_in_db(
                        node_name=client_id,
                        node_type=metadata.get("type", "unknown"),
                        instance_key=metadata.get("instance_key", client_id),
                        host=metadata.get("host"),
                        port=metadata.get("port")
                    )
                except Exception as e:
                    self.logger.error(f"Failed to update database for client {client_id}: {e}")
            
            return {
                "status": "success",
                "message": "Registration successful",
                "client_id": client_id,
                "server_info": {
                    "pub_address": f"{self.protocol}://{self.bind_address}:{self.port + 1}",
                    "pull_address": f"{self.protocol}://{self.bind_address}:{self.port + 2}"
                }
            }
    
    def _handle_unregister(self, client_id: str):
        """Handle client unregistration"""
        with self.clients_lock:
            if client_id in self.clients:
                del self.clients[client_id]
                self.logger.info(f"Client {client_id} unregistered")
                return {"status": "success", "message": "Unregistration successful"}
            else:
                return {"status": "error", "message": "Client not found"}
    
    def _handle_heartbeat(self, client_id: str):
        """Handle client heartbeat"""
        with self.clients_lock:
            if client_id in self.clients:
                self.clients[client_id].last_seen = time.time()
                
                # Update database if enabled
                if self.db_enabled:
                    try:
                        client_info = self.clients[client_id]
                        self._update_node_in_db(
                            node_name=client_id,
                            node_type=client_info.metadata.get("type", "unknown"),
                            instance_key=client_info.metadata.get("instance_key", client_id),
                            host=client_info.metadata.get("host"),
                            port=client_info.metadata.get("port")
                        )
                    except Exception as e:
                        self.logger.error(f"Failed to update database for heartbeat {client_id}: {e}")
                
                return {"status": "success", "message": "Heartbeat received"}
            else:
                return {"status": "error", "message": "Client not found"}
    
    def _handle_im_alive(self, client_id: str, data: Dict[str, Any]):
        """Handle im_alive message (compatible with leader API)"""
        node_name = data.get("node_name")
        node_type = data.get("node_type")
        instance_key = data.get("instance_key")
        host = data.get("host")
        port = data.get("port")
        
        if not all([node_name, node_type, instance_key]):
            return {"status": "error", "message": "Missing required fields: node_name, node_type, instance_key"}
        
        # Update database if enabled
        if self.db_enabled:
            try:
                success = self._update_node_in_db(node_name, node_type, instance_key, host, port)
                if success:
                    self.logger.info(f"Updated node {node_name} in database via im_alive")
                    return {"status": "received"}
                else:
                    return {"status": "error", "message": "Failed to update node in database"}
            except Exception as e:
                self.logger.error(f"Failed to update database for im_alive {node_name}: {e}")
                return {"status": "error", "message": f"Database error: {e}"}
        else:
            return {"status": "received", "message": "Database not available"}
    
    def _update_node_in_db(self, node_name: str, node_type: str, instance_key: str, 
                          host: Optional[str] = None, port: Optional[int] = None) -> bool:
        """Update node information in database using the global db_manager"""
        try:
            # Use the last_seen table structure from the existing db_manager
            current_time = int(time.time())
            
            query = """
                INSERT INTO last_seen (name, type, key, last_update)
                VALUES (%s, %s, %s, %s)
                ON CONFLICT (key) DO UPDATE SET
                    name = EXCLUDED.name,
                    type = EXCLUDED.type,
                    last_update = EXCLUDED.last_update
            """
            
            self.db_manager.execute_query(query, (node_name, node_type, instance_key, current_time))
            self.logger.info(f"Updated node {node_name} in database")
            return True
            
        except Exception as e:
            self.logger.error(f"Failed to update node {node_name} in database: {e}")
            return False
    
    def _handle_message(self, from_client: str, to_client: str, message: Dict[str, Any]):
        """Handle direct message between clients"""
        with self.clients_lock:
            if to_client not in self.clients:
                return {"status": "error", "message": "Target client not found"}
            
            # Send message to target client
            try:
                response = {
                    "type": MessageType.MESSAGE.value,
                    "from": from_client,
                    "message": message
                }
                self.router_socket.send_multipart([
                    self.clients[to_client].id.encode(),
                    json.dumps(response).encode()
                ])
                return {"status": "success", "message": "Message sent"}
            except Exception as e:
                self.logger.error(f"Failed to send message: {e}")
                return {"status": "error", "message": "Failed to send message"}
    
    def _handle_broadcast(self, from_client: str, message: Dict[str, Any]):
        """Handle broadcast message"""
        try:
            response = {
                "type": MessageType.BROADCAST.value,
                "from": from_client,
                "message": message
            }
            self.pub_socket.send_string(json.dumps(response))
            return {"status": "success", "message": "Broadcast sent"}
        except Exception as e:
            self.logger.error(f"Failed to broadcast message: {e}")
            return {"status": "error", "message": "Failed to broadcast message"}
    
    def _handle_peer_discovery(self, client_id: str):
        """Handle peer discovery request"""
        with self.clients_lock:
            peers = []
            for cid, client in self.clients.items():
                if cid != client_id:
                    peers.append({
                        "id": client.id,
                        "address": client.address,
                        "metadata": client.metadata
                    })
            
            return {
                "status": "success",
                "peers": peers
            }
    
    def _get_alive_nodes_from_db(self):
        """Get alive nodes from database using the global db_manager"""
        if not self.db_enabled:
            return []
        
        try:
            timeout = self.config.get("message_storage", {}).get("alive_timeout", 10)
            cutoff_time = int(time.time()) - timeout
            
            query = """
                SELECT name, type, key, last_update
                FROM last_seen
                WHERE last_update > %s
                ORDER BY name
            """
            
            results = self.db_manager.execute_query(query, (cutoff_time,), fetch_all=True)
            
            nodes = []
            for row in results:
                nodes.append({
                    'name': row['name'],
                    'type': row['type'],
                    'instance_key': row['key'],
                    'last_seen': row['last_update']
                })
            
            return nodes
            
        except Exception as e:
            self.logger.error(f"Failed to get alive nodes from database: {e}")
            return []
    
    def _get_required_nodes(self):
        """Get required nodes from configuration"""
        return self.config.get("message_storage", {}).get("required_nodes", [])
    
    def _cleanup_dead_clients(self):
        """Remove clients that haven't sent heartbeat"""
        current_time = time.time()
        with self.clients_lock:
            dead_clients = []
            for client_id, client in self.clients.items():
                if current_time - client.last_seen > self.timeout:
                    dead_clients.append(client_id)
            
            for client_id in dead_clients:
                del self.clients[client_id]
                self.logger.info(f"Removed dead client {client_id}")
    
    def _process_router_message(self):
        """Process messages from ROUTER socket"""
        try:
            # Non-blocking receive
            message_parts = self.router_socket.recv_multipart(flags=zmq.NOBLOCK)
            if len(message_parts) >= 2:
                client_id = message_parts[0].decode()
                message_data = json.loads(message_parts[1].decode())
                
                msg_type = message_data.get("type")
                response = {"status": "error", "message": "Unknown message type"}
                
                if msg_type == MessageType.REGISTER.value:
                    response = self._handle_register(
                        client_id,
                        message_data.get("address", ""),
                        message_data.get("metadata", {})
                    )
                
                elif msg_type == MessageType.UNREGISTER.value:
                    response = self._handle_unregister(client_id)
                
                elif msg_type == MessageType.HEARTBEAT.value:
                    response = self._handle_heartbeat(client_id)
                
                elif msg_type == MessageType.IM_ALIVE.value:
                    response = self._handle_im_alive(client_id, message_data)
                
                elif msg_type == MessageType.MESSAGE.value:
                    response = self._handle_message(
                        client_id,
                        message_data.get("to"),
                        message_data.get("message", {})
                    )
                
                elif msg_type == MessageType.BROADCAST.value:
                    response = self._handle_broadcast(
                        client_id,
                        message_data.get("message", {})
                    )
                
                elif msg_type == MessageType.PEER_DISCOVERY.value:
                    response = self._handle_peer_discovery(client_id)
                
                # Send response back to client
                self.router_socket.send_multipart([
                    client_id.encode(),
                    json.dumps(response).encode()
                ])
                
        except zmq.Again:
            # No message available
            pass
        except Exception as e:
            self.logger.error(f"Error processing router message: {e}")
    
    def _process_pull_message(self):
        """Process messages from PULL socket"""
        try:
            # Non-blocking receive
            message_data = self.pull_socket.recv_string(flags=zmq.NOBLOCK)
            message = json.loads(message_data)
            
            msg_type = message.get("type")
            if msg_type == MessageType.BROADCAST.value:
                # Broadcast to all clients
                with self.clients_lock:
                    for client_id in self.clients:
                        try:
                            self.router_socket.send_multipart([
                                client_id.encode(),
                                message_data.encode()
                            ])
                        except Exception as e:
                            self.logger.error(f"Failed to send broadcast to {client_id}: {e}")
            
        except zmq.Again:
            # No message available
            pass
        except Exception as e:
            self.logger.error(f"Error processing pull message: {e}")
    
    def _heartbeat_worker(self):
        """Background worker for cleanup and heartbeat processing"""
        while self.running:
            try:
                self._cleanup_dead_clients()
                time.sleep(self.heartbeat_interval)
            except Exception as e:
                self.logger.error(f"Error in heartbeat worker: {e}")
    
    def start(self):
        """Start the ZeroMQ signaling server"""
        self.running = True
        self.logger.info("Starting ZeroMQ signaling server...")
        
        # Start heartbeat worker
        heartbeat_thread = threading.Thread(target=self._heartbeat_worker, daemon=True)
        heartbeat_thread.start()
        
        # Main server loop
        try:
            while self.running:
                # Process router messages
                self._process_router_message()
                
                # Process pull messages
                self._process_pull_message()
                
                # Small sleep to prevent busy waiting
                time.sleep(0.001)
                
        except KeyboardInterrupt:
            self.logger.info("Received keyboard interrupt, shutting down...")
        except Exception as e:
            self.logger.error(f"Unexpected error: {e}")
            raise
        finally:
            self.stop()
    
    def stop(self):
        """Stop the ZeroMQ signaling server"""
        self.running = False
        self.logger.info("Stopping ZeroMQ signaling server...")
        
        # Close sockets
        for socket in [self.router_socket, self.pub_socket, self.pull_socket]:
            try:
                socket.close()
            except Exception as e:
                self.logger.error(f"Error closing socket: {e}")
        
        # Shutdown database connection
        if self.db_enabled and hasattr(self, 'db_manager'):
            try:
                self.db_manager.shutdown()
            except Exception as e:
                self.logger.error(f"Error shutting down database: {e}")
        
        # Terminate context
        try:
            self.context.term()
        except Exception as e:
            self.logger.error(f"Error terminating context: {e}")
        
        self.logger.info("ZeroMQ signaling server stopped") 