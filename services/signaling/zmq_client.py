import zmq
import json
import time
import threading
import uuid
import logging
from typing import Dict, Any, Optional, Callable
from dataclasses import dataclass

@dataclass
class Message:
    """Message structure for client communication"""
    type: str
    data: Dict[str, Any]
    timestamp: float

class ZeroMQSignalingClient:
    """ZeroMQ signaling client for peer-to-peer communication"""
    
    def __init__(self, server_address: str, client_id: Optional[str] = None):
        self.server_address = server_address
        self.client_id = client_id or str(uuid.uuid4())
        self.logger = logging.getLogger(__name__)
        
        # ZeroMQ context
        self.context = zmq.Context()
        
        # Sockets
        self.dealer_socket = None
        self.sub_socket = None
        self.push_socket = None
        
        # Client state
        self.connected = False
        self.running = False
        
        # Message handlers
        self.message_handlers: Dict[str, Callable] = {}
        
        # Setup sockets
        self._setup_sockets()
    
    def _setup_sockets(self):
        """Setup client sockets"""
        try:
            # DEALER socket for request-reply communication
            self.dealer_socket = self.context.socket(zmq.DEALER)
            self.dealer_socket.setsockopt_string(zmq.IDENTITY, self.client_id)
            
            # SUB socket for receiving broadcasts
            self.sub_socket = self.context.socket(zmq.SUB)
            self.sub_socket.setsockopt_string(zmq.SUBSCRIBE, "")
            
            # PUSH socket for sending messages to server
            self.push_socket = self.context.socket(zmq.PUSH)
            
            self.logger.info(f"Client sockets setup for {self.client_id}")
            
        except Exception as e:
            self.logger.error(f"Failed to setup client sockets: {e}")
            raise
    
    def connect(self, metadata: Optional[Dict[str, Any]] = None):
        """Connect to the signaling server"""
        try:
            # Connect to server sockets
            self.dealer_socket.connect(f"{self.server_address}")
            self.sub_socket.connect(f"{self.server_address}:5556")  # PUB socket
            self.push_socket.connect(f"{self.server_address}:5557")  # PULL socket
            
            # Register with server
            register_message = {
                "type": "register",
                "address": f"client_{self.client_id}",
                "metadata": metadata or {}
            }
            
            self.dealer_socket.send_string(json.dumps(register_message))
            response = self.dealer_socket.recv_string()
            response_data = json.loads(response)
            
            if response_data.get("status") == "success":
                self.connected = True
                self.logger.info(f"Successfully connected to signaling server")
                return True
            else:
                self.logger.error(f"Failed to connect: {response_data.get('message')}")
                return False
                
        except Exception as e:
            self.logger.error(f"Failed to connect to server: {e}")
            return False
    
    def disconnect(self):
        """Disconnect from the signaling server"""
        if self.connected:
            try:
                # Unregister from server
                unregister_message = {
                    "type": "unregister"
                }
                self.dealer_socket.send_string(json.dumps(unregister_message))
                self.connected = False
                self.logger.info("Disconnected from signaling server")
            except Exception as e:
                self.logger.error(f"Error during disconnect: {e}")
    
    def send_message(self, to_client: str, message: Dict[str, Any]):
        """Send direct message to another client"""
        if not self.connected:
            self.logger.error("Not connected to server")
            return False
        
        try:
            msg = {
                "type": "message",
                "to": to_client,
                "message": message
            }
            self.dealer_socket.send_string(json.dumps(msg))
            response = self.dealer_socket.recv_string()
            response_data = json.loads(response)
            
            if response_data.get("status") == "success":
                self.logger.info(f"Message sent to {to_client}")
                return True
            else:
                self.logger.error(f"Failed to send message: {response_data.get('message')}")
                return False
                
        except Exception as e:
            self.logger.error(f"Error sending message: {e}")
            return False
    
    def broadcast(self, message: Dict[str, Any]):
        """Broadcast message to all clients"""
        if not self.connected:
            self.logger.error("Not connected to server")
            return False
        
        try:
            msg = {
                "type": "broadcast",
                "message": message
            }
            self.push_socket.send_string(json.dumps(msg))
            self.logger.info("Broadcast message sent")
            return True
            
        except Exception as e:
            self.logger.error(f"Error broadcasting message: {e}")
            return False
    
    def discover_peers(self):
        """Discover other connected peers"""
        if not self.connected:
            self.logger.error("Not connected to server")
            return []
        
        try:
            msg = {
                "type": "peer_discovery"
            }
            self.dealer_socket.send_string(json.dumps(msg))
            response = self.dealer_socket.recv_string()
            response_data = json.loads(response)
            
            if response_data.get("status") == "success":
                peers = response_data.get("peers", [])
                self.logger.info(f"Discovered {len(peers)} peers")
                return peers
            else:
                self.logger.error(f"Failed to discover peers: {response_data.get('message')}")
                return []
                
        except Exception as e:
            self.logger.error(f"Error discovering peers: {e}")
            return []
    
    def send_heartbeat(self):
        """Send heartbeat to server"""
        if not self.connected:
            return False
        
        try:
            msg = {
                "type": "heartbeat"
            }
            self.dealer_socket.send_string(json.dumps(msg))
            response = self.dealer_socket.recv_string()
            response_data = json.loads(response)
            
            return response_data.get("status") == "success"
            
        except Exception as e:
            self.logger.error(f"Error sending heartbeat: {e}")
            return False
    
    def register_message_handler(self, message_type: str, handler: Callable):
        """Register a message handler"""
        self.message_handlers[message_type] = handler
    
    def _handle_incoming_messages(self):
        """Handle incoming messages from server"""
        while self.running:
            try:
                # Check for messages from dealer socket
                try:
                    message = self.dealer_socket.recv_string(flags=zmq.NOBLOCK)
                    message_data = json.loads(message)
                    self._process_message(message_data)
                except zmq.Again:
                    pass
                
                # Check for broadcast messages from sub socket
                try:
                    broadcast = self.sub_socket.recv_string(flags=zmq.NOBLOCK)
                    broadcast_data = json.loads(broadcast)
                    self._process_message(broadcast_data)
                except zmq.Again:
                    pass
                
                time.sleep(0.001)
                
            except Exception as e:
                self.logger.error(f"Error handling incoming messages: {e}")
    
    def _process_message(self, message_data: Dict[str, Any]):
        """Process incoming message"""
        msg_type = message_data.get("type")
        
        if msg_type in self.message_handlers:
            try:
                self.message_handlers[msg_type](message_data)
            except Exception as e:
                self.logger.error(f"Error in message handler for {msg_type}: {e}")
        else:
            self.logger.debug(f"Received message of type {msg_type}: {message_data}")
    
    def _heartbeat_worker(self):
        """Background worker for sending heartbeats"""
        while self.running:
            if self.connected:
                self.send_heartbeat()
            time.sleep(30)  # Send heartbeat every 30 seconds
    
    def start(self):
        """Start the client message processing"""
        if not self.connected:
            self.logger.error("Not connected to server")
            return
        
        self.running = True
        self.logger.info("Starting client message processing...")
        
        # Start message handling thread
        message_thread = threading.Thread(target=self._handle_incoming_messages, daemon=True)
        message_thread.start()
        
        # Start heartbeat thread
        heartbeat_thread = threading.Thread(target=self._heartbeat_worker, daemon=True)
        heartbeat_thread.start()
        
        try:
            while self.running:
                time.sleep(1)
        except KeyboardInterrupt:
            self.logger.info("Received keyboard interrupt, shutting down...")
        finally:
            self.stop()
    
    def stop(self):
        """Stop the client"""
        self.running = False
        self.disconnect()
        
        # Close sockets
        for socket in [self.dealer_socket, self.sub_socket, self.push_socket]:
            if socket:
                try:
                    socket.close()
                except Exception as e:
                    self.logger.error(f"Error closing socket: {e}")
        
        # Terminate context
        try:
            self.context.term()
        except Exception as e:
            self.logger.error(f"Error terminating context: {e}")
        
        self.logger.info("Client stopped")

# Example usage
if __name__ == "__main__":
    import logging
    
    # Setup logging
    logging.basicConfig(level=logging.INFO)
    
    # Create client
    client = ZeroMQSignalingClient("tcp://localhost:5555")
    
    # Register message handlers
    def handle_message(message_data):
        print(f"Received message from {message_data.get('from')}: {message_data.get('message')}")
    
    def handle_broadcast(message_data):
        print(f"Received broadcast from {message_data.get('from')}: {message_data.get('message')}")
    
    client.register_message_handler("message", handle_message)
    client.register_message_handler("broadcast", handle_broadcast)
    
    # Connect to server
    if client.connect({"name": "test_client", "type": "example"}):
        print(f"Connected with client ID: {client.client_id}")
        
        # Discover peers
        peers = client.discover_peers()
        print(f"Found peers: {peers}")
        
        # Start client
        client.start()
    else:
        print("Failed to connect to server") 