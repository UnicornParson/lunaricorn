import zmq
import json
import time
import logging
import threading
from typing import Dict, Any, List, Callable, Optional
from dataclasses import dataclass, asdict
from typing import Callable
from enum import Enum
from datetime import datetime
import requests

@dataclass
class SignalingClientConfig:
    host: str
    rep_port: int
    pub_port: int
    api_port: int

    def __str__(self) -> str:
        return f"{self.host}(rep:{self.rep_port}, pub:{self.pub_port}, api:{self.api_port})"
@dataclass
class ClientEventData:
    eid: int
    event_type: str
    payload: Dict[str, Any]
    timestamp: float
    source: Optional[str] = None
    affected: Optional[list]= None
    tags: Optional[list]= None
    @classmethod
    def from_dict(cls, data: dict) -> 'ClientEventData':
        try:
            timestamp = data['timestamp'].timestamp() if isinstance(data['timestamp'], datetime) else data['timestamp']
            return cls(
                eid=data['eid'],
                event_type=data['event_type'],
                payload=data['payload'],
                timestamp=timestamp,
                source=data.get('source'),
                affected=data.get('affected'),
                tags=data.get('tags')
            )
        except Exception as e:
            logger = logging.getLogger(f"{__name__}.ClientEventData.from_dict")
            logger.error(f"error {e} for data \n {data}")
            raise e
    
    def __str__(self) -> str:
        affected_str = f", affected={self.affected}" if self.affected is not None else ""
        tags_str = f", tags={self.tags}" if self.tags is not None else ""
        source_str = f", source={self.source}" if self.source is not None else ""
        return (
            f"EventData("
            f"event_type={self.event_type}, "
            f"payload={self.payload}, "
            f"timestamp={self.timestamp}"
            f"{source_str}{affected_str}{tags_str}"
            f")"
        )

class SignalingClient:
    EVENT_FILTER_ANY = "*"
    def __init__(self, config: SignalingClientConfig, client_id):
        """
        Initialize ZeroMQ client with configuration.
        
        :param config: Client configuration dictionary
        :param client_id: Unique client identifier (auto-generated if not provided)
        """
        self.config = config
        self.client_id = client_id
        self.logger = logging.getLogger(f"{__name__}.cid#{client_id}")
        self.net_timeout = 3000
        self.poll_timeout = 1000
        # ZeroMQ context
        self.context = None
        
        # Server connection details
        self.server_host = config.host
        self.rep_port = config.rep_port  # REQ-REP port
        self.pub_port = config.rep_port  # PUB-SUB port
        self.api_port = config.api_port  # rest api port
        self.protocol = "tcp"
        
        # Client state management
        self.connected = False
        self.subscribed_event_types = set()
        self.event_handlers = {}
        
        # ZeroMQ sockets
        self.req_socket = None  # REQ socket for control messages
        self.sub_socket = None  # SUB socket for event reception
        
        # Heartbeat configuration
        self.heartbeat_interval = 10
        self.heartbeat_thread = None
        self.running = False
        
        # Thread synchronization
        self.lock = threading.Lock()
        self.subscribe_callback = None
    
    def set_event_callback(self, subscribe_callback: Callable[[ClientEventData], None]):
        self.subscribe_callback = subscribe_callback

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
            self.req_socket.setsockopt(zmq.RCVTIMEO, self.net_timeout)  # 5 second receive timeout
            
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

            self.watched_types = []
            return True
            
        except Exception as e:
            self.logger.error(f"Failed to connect to server: {e}")
            self.connected = False
            self.running = False
            return False
    def __repr__(self) -> str:
        return self.__str__() 
    def __str__(self) -> str:
        status = "Connected" if self.connected else "Disconnected"
        state = "running" if self.running else "stopped"
        return f"Lunaricorn signaling client {status} {state}"

    def subscribe(self, event_types: List[str]):
        for et in event_types:
            if et not in self.watched_types:
                self.watched_types.append(et)

    def unsubscribe(self, event_types: List[str]):
        for et in event_types:
            self.watched_types.remove(et)

    def _event_wanted(self, event:ClientEventData) -> bool:
        for wt in self.watched_types:
            if wt == SignalingClient.EVENT_FILTER_ANY or event.event_type == wt:
                return True
        return False

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
    def _send_heartbeat(self) -> Dict[str, Any]:
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
                if self.sub_socket.poll(self.poll_timeout):  # 1 second timeout
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

        required_fields = ["type", "payload", "eid"]
        for field in required_fields:
            if field not in event_data:
                self.logger.error(f"Missing required field: {field}")
                return {"status": "error", "message": f"Missing required field: {field}"}

        data = ClientEventData(
            eid=event_data["eid"],
            event_type=event_data["type"],
            payload=event_data["payload"],
            timestamp=event_data.get("timestamp", time.time()),
            source=event_data.get("creator-id"),
            affected=event_data.get("affected"),
            tags=event_data.get("tags")
        )
        if not self._event_wanted():
            self.logger.info(f"ignore event {data.event_type}")
            return
        if subscribe_callback:
            subscribe_callback(data)
        else:
            self.logger.error(f"no subscribe_callback")

    
    def _start_heartbeat(self):
        """Start periodic heartbeat transmission to maintain connection."""
        def heartbeat_loop():
            while self.running and self.connected:
                try:
                    self._send_heartbeat()
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

    def browse_events(self,
                     timestamp: float = 0,
                     event_types: Optional[List[str]] = None,
                     sources: Optional[List[str]] = None,
                     affected: Optional[List[str]] = None,
                     tags: Optional[List[str]] = None,
                     limit: Optional[int] = None) -> List[ClientEventData]:

        url = f"http://{self.server_host}:{self.api_port}/v1/browse"
        request_body = {"timestamp": timestamp}
        
        if event_types:
            request_body["event_types"] = event_types
        if sources:
            request_body["sources"] = sources
        if affected:
            request_body["affected"] = affected
        if tags:
            request_body["tags"] = tags
        if limit:
            request_body["limit"] = limit

        try:
            response = requests.post(
                url,
                json=request_body,
                headers={
                    "Content-Type": "application/json",
                    "Accept-Encoding": "br, gzip, deflate"
                },
                timeout=self.net_timeout
            )
            response.raise_for_status()

            events_data = response.json()
            return [ClientEventData.from_dict(event) for event in events_data]
            
        except requests.exceptions.RequestException as e:
            raise Exception(f"API request failed: {str(e)}")
        except ValueError as e:
            raise Exception(f"Failed to parse response: {str(e)}")
        
    def get_tags_list(self) -> List[str]:
        """
        Retrieve All Available Tags
        
        Returns an array of all unique tags used in the system for event categorization
        
        :return: List of tag names
        :raises: Exception if API request fails
        """
        return self._get_list("tags")
    
    def get_types_list(self) -> List[str]:
        """
        Retrieve All Available Types
        
        Returns an array of all unique types used in the system for event categorization
        
        :return: List of type names
        :raises: Exception if API request fails
        """
        return self._get_list("types")
    
    def get_affected_list(self) -> List[str]:
        """
        Retrieve All Available affected objects
        
        Returns an array of all unique affected objects used in the system for event categorization
        
        :return: List of affected object names
        :raises: Exception if API request fails
        """
        return self._get_list("affected")
    
    def get_owners_list(self) -> List[str]:
        """
        Retrieve All Available affected owners
        
        Returns an array of all unique owner names used in the system for event categorization
        
        :return: List of owner names
        :raises: Exception if API request fails
        """
        return self._get_list("owners")
    
    def _get_list(self, endpoint: str) -> List[str]:
        """
        Internal method to handle all list API requests
        
        :param endpoint: API endpoint suffix (tags, types, affected, owners)
        :return: List of strings from the API response
        :raises: Exception if API request fails
        """
        url = f"http://{self.server_host}:{self.api_port}/v1/list/{endpoint}"
        
        try:
            response = requests.get(url, timeout=10)
            response.raise_for_status()
            return response.json()
        except requests.exceptions.RequestException as e:
            raise Exception(f"API request failed: {str(e)}")
        except ValueError as e:
            raise Exception(f"Failed to parse response: {str(e)}")