import zmq
import json
import time
import logging
import threading
import uuid
from typing import Dict, Any, List, Callable, Optional

class SignalingClient:
    """
    Client for ZeroMQ Signaling Server communication.
    Supports event subscription, heartbeat mechanism, and event notification handling.
    """
    
    def __init__(self, config: Dict[str, Any], client_id: str = None):
        """
        Initialize ZeroMQ client with configuration.
        
        :param config: Client configuration dictionary
        :param client_id: Unique client identifier (auto-generated if not provided)
        """
        self.config = config
        self.client_id = client_id or str(uuid.uuid4())
        self.logger = logging.getLogger(__name__)
        
        # ZeroMQ context
        self.context = None
        
        # Server connection details
        self.server_host = config.get("server_host", "localhost")
        self.rep_port = config.get("rep_port", 5555)  # REQ-REP port
        self.pub_port = config.get("pub_port", 5556)  # PUB-SUB port
        self.protocol = config.get("protocol", "tcp")
        
        # Client state management
        self.connected = False
        self.subscribed_event_types = set()
        self.event_handlers = {}
        
        # ZeroMQ sockets
        self.req_socket = None  # REQ socket for control messages
        self.sub_socket = None  # SUB socket for event reception
        
        # Heartbeat configuration
        self.heartbeat_interval = config.get("heartbeat_interval", 30)
        self.heartbeat_thread = None
        self.running = False
        
        # Thread synchronization
        self.lock = threading.Lock()
    
    def connect(self) -> bool:
        """
        Establish connection to ZeroMQ signaling server.
        
        :return: True if connection successful, False otherwise
        """
        try:
            # Create ZeroMQ context
            self.context = zmq.Context()
            
            # Create REQ socket for control messages (subscribe, unsubscribe, heartbeat)
            self.req_socket = self.context.socket(zmq.REQ)
            # Set socket options for better reliability
            self.req_socket.setsockopt(zmq.LINGER, 0)  # Don't linger on close
            self.req_socket.setsockopt(zmq.RCVTIMEO, 5000)  # 5 second receive timeout
            
            rep_address = f"{self.protocol}://{self.server_host}:{self.rep_port}"
            self.req_socket.connect(rep_address)
            self.logger.info(f"Connected to REP socket at {rep_address}")
            
            # Create SUB socket for event reception
            self.sub_socket = self.context.socket(zmq.SUB)
            # Set socket options for better reliability
            self.sub_socket.setsockopt(zmq.LINGER, 0)  # Don't linger on close
            
            pub_address = f"{self.protocol}://{self.server_host}:{self.pub_port}"
            self.sub_socket.connect(pub_address)
            self.logger.info(f"Connected to PUB socket at {pub_address}")
            
            # Subscribe to all messages (empty filter)
            self.sub_socket.setsockopt_string(zmq.SUBSCRIBE, "")
            
            # Update connection state
            self.connected = True
            self.running = True
            
            # Start event reception thread
            self.receive_thread = threading.Thread(target=self._receive_events, daemon=True)
            self.receive_thread.start()
            
            # Start heartbeat mechanism
            self._start_heartbeat()
            
            self.logger.info(f"ZeroMQ client {self.client_id} connected successfully")
            return True
            
        except Exception as e:
            self.logger.error(f"Failed to connect to server: {e}")
            self.connected = False
            self.running = False
            return False
    
    def subscribe(self, event_types: List[str]) -> Dict[str, Any]:
        """
        Subscribe to specific event types.
        
        :param event_types: List of event types to subscribe to
        :return: Server response dictionary
        """
        if not self.connected:
            raise ConnectionError("Client is not connected to server")
            
        message = {
            "type": "subscribe",
            "client_id": self.client_id,
            "event_types": event_types
        }
        
        response = self._send_request(message)
        
        if response.get("status") == "success":
            self.subscribed_event_types.update(event_types)
            
        return response
    
    def unsubscribe(self, event_types: List[str]) -> Dict[str, Any]:
        """
        Unsubscribe from specific event types.
        
        :param event_types: List of event types to unsubscribe from
        :return: Server response dictionary
        """
        if not self.connected:
            raise ConnectionError("Client is not connected to server")
            
        message = {
            "type": "unsubscribe",
            "client_id": self.client_id,
            "event_types": event_types
        }
        
        response = self._send_request(message)
        
        if response.get("status") == "success":
            for event_type in event_types:
                if event_type in self.subscribed_event_types:
                    self.subscribed_event_types.remove(event_type)
            
        return response
    def push_event(self, event_type: str, payload: Dict[str, Any], 
                  source: Optional[str] = None, tags: Optional[List] = None) -> Dict[str, Any]:
        """
        Send a push event to the server.
        
        :param event_type: Type of the event
        :param payload: Event payload data
        :param source: Source of the event (optional)
        :param tags: Event tags (optional)
        :return: Server response dictionary
        """
        if not self.connected:
            raise ConnectionError("Client is not connected to server")
            
        message = {
            "type": "push",
            "client_id": self.client_id,
            "event_type": event_type,
            "message": payload,
            "timestamp": time.time()
        }
        
        if source:
            message["source"] = source
            
        if tags:
            message["tags"] = tags
            
        return self._send_request(message)
    def send_heartbeat(self) -> Dict[str, Any]:
        """
        Send heartbeat message to server to maintain connection.
        
        :return: Server response dictionary
        """
        if not self.connected:
            raise ConnectionError("Client is not connected to server")
            
        message = {
            "type": "heartbeat",
            "client_id": self.client_id
        }
        
        return self._send_request(message)
    
    def add_event_handler(self, event_type: str, handler: Callable):
        """
        Register handler function for specific event type.
        
        :param event_type: Event type to handle
        :param handler: Callback function to process event
        """
        if event_type not in self.event_handlers:
            self.event_handlers[event_type] = []
        self.event_handlers[event_type].append(handler)
    
    def _send_request(self, message: Dict[str, Any]) -> Dict[str, Any]:
        """
        Send request to server and receive response.
        
        :param message: Request message dictionary
        :return: Response dictionary from server
        """
        with self.lock:  # Ensure thread-safe access to REQ socket
            try:
                print("Send request")
                self.req_socket.send_string(json.dumps(message))
                
                # Wait for response with timeout
                if self.req_socket.poll(5000):  # 5 second timeout
                    response_str = self.req_socket.recv_string()
                    return json.loads(response_str)
                else:
                    self.logger.warning("Request timeout, reconnecting...")
                    # Try to reconnect
                    if self._reconnect():
                        # Retry the request
                        self.req_socket.send_string(json.dumps(message))
                        if self.req_socket.poll(5000):
                            response_str = self.req_socket.recv_string()
                            return json.loads(response_str)
                    return {"status": "error", "message": "Request timeout after reconnect attempt"}
                    
            except zmq.ZMQError as e:
                self.logger.error(f"ZMQ error sending request: {e}")
                # Try to reconnect
                if self._reconnect():
                    # Retry the request
                    try:
                        self.req_socket.send_string(json.dumps(message))
                        if self.req_socket.poll(5000):
                            response_str = self.req_socket.recv_string()
                            return json.loads(response_str)
                    except Exception as retry_error:
                        self.logger.error(f"Retry failed: {retry_error}")
                
                return {"status": "error", "message": f"ZMQ error: {e}"}
            except Exception as e:
                self.logger.error(f"Error sending request: {e}")
                return {"status": "error", "message": f"Request failed: {e}"}
    
    def _reconnect(self) -> bool:
        """
        Attempt to reconnect to the server.
        
        :return: True if reconnection successful, False otherwise
        """
        self.logger.info("Attempting to reconnect to server...")
        self.disconnect()
        time.sleep(1)  # Brief delay before reconnecting
        return self.connect()
    
    def _receive_events(self):
        """Background thread for receiving events from server."""
        while self.running and self.connected:
            try:
                # Check for incoming events with timeout
                if self.sub_socket.poll(1000):  # 1 second timeout
                    event_str = self.sub_socket.recv_string()
                    event_data = json.loads(event_str)
                    
                    # Process received event
                    self._handle_event(event_data)
                    
            except zmq.ZMQError as e:
                if self.running:  # Only log errors if client is still running
                    self.logger.error(f"ZMQ error receiving event: {e}")
                    # Try to reconnect
                    if not self._reconnect():
                        break
            except Exception as e:
                if self.running:  # Only log errors if client is still running
                    self.logger.error(f"Error receiving event: {e}")
    
    def _handle_event(self, event_data: Dict[str, Any]):
        """
        Process incoming event by calling registered handlers.
        
        :param event_data: Event data dictionary
        """
        event_type = event_data.get("type")
        
        # Call specific handlers for this event type
        if event_type and event_type in self.event_handlers:
            for handler in self.event_handlers[event_type]:
                try:
                    handler(event_data)
                except Exception as e:
                    self.logger.error(f"Error in event handler for {event_type}: {e}")
        
        # Call wildcard handlers if registered
        if "*" in self.event_handlers:
            for handler in self.event_handlers["*"]:
                try:
                    handler(event_data)
                except Exception as e:
                    self.logger.error(f"Error in wildcard event handler: {e}")
    
    def _start_heartbeat(self):
        """Start periodic heartbeat transmission to maintain connection."""
        def heartbeat_loop():
            while self.running and self.connected:
                try:
                    self.send_heartbeat()
                    self.logger.debug(f"Heartbeat sent from {self.client_id}")
                except Exception as e:
                    self.logger.error(f"Heartbeat failed: {e}")
                    # Try to reconnect if heartbeat fails
                    if not self._reconnect():
                        break
                
                # Wait for next heartbeat
                for _ in range(self.heartbeat_interval):
                    if not self.running:
                        break
                    time.sleep(1)
        
        self.heartbeat_thread = threading.Thread(target=heartbeat_loop, daemon=True)
        self.heartbeat_thread.start()
    
    def disconnect(self):
        """Cleanly disconnect from server and release resources."""
        self.running = False
        self.connected = False
        
        # Close sockets
        try:
            if self.req_socket:
                self.req_socket.close()
        except Exception as e:
            self.logger.error(f"Error closing REQ socket: {e}")
        
        try:
            if self.sub_socket:
                self.sub_socket.close()
        except Exception as e:
            self.logger.error(f"Error closing SUB socket: {e}")
        
        # Terminate context
        try:
            if self.context:
                self.context.term()
        except Exception as e:
            self.logger.error(f"Error terminating context: {e}")
        
        self.logger.info(f"ZeroMQ client {self.client_id} disconnected")