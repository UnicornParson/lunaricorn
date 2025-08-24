import zmq
import json
import threading
import time
import logging
import uuid
from typing import Dict, Any, Callable, List, Set
from collections import defaultdict
from dataclasses import dataclass
from enum import Enum

class ZeroMQSignalingClient:
    """ZeroMQ client for signaling server"""
    
    def __init__(self, server_host: str, rep_port: int = 5555, pub_port: int = 5556):
        self.server_host = server_host
        self.rep_port = rep_port
        self.pub_port = pub_port
        self.client_id = f"client_{uuid.uuid4().hex[:8]}"
        
        self.context = zmq.Context()
        self.logger = logging.getLogger(__name__)
        
        # Setup REQ socket for requests
        self.req_socket = self.context.socket(zmq.REQ)
        self.req_socket.connect(f"tcp://{server_host}:{rep_port}")
        
        # Setup SUB socket for events
        self.sub_socket = self.context.socket(zmq.SUB)
        self.sub_socket.connect(f"tcp://{server_host}:{pub_port}")
        
        # Event handlers
        self.event_handlers = defaultdict(list)
        
        # Start event listener thread
        self.running = False
        self.listener_thread = None
    
    def subscribe(self, event_types: List[str]):
        """Subscribe to specific event types"""
        subscription_msg = {
            "type": "subscribe",
            "client_id": self.client_id,
            "event_types": event_types
        }
        
        self.req_socket.send_string(json.dumps(subscription_msg))
        response = self.req_socket.recv_string()
        response_data = json.loads(response)
        
        if response_data.get("status") == "success":
            self.logger.info(f"Successfully subscribed to: {event_types}")
            
            # Subscribe to all event types on SUB socket
            for event_type in event_types:
                self.sub_socket.setsockopt_string(zmq.SUBSCRIBE, event_type)
        else:
            self.logger.error(f"Failed to subscribe: {response_data.get('message')}")
    
    def unsubscribe(self, event_types: List[str]):
        """Unsubscribe from specific event types"""
        unsubscribe_msg = {
            "type": "unsubscribe",
            "client_id": self.client_id,
            "event_types": event_types
        }
        
        self.req_socket.send_string(json.dumps(unsubscribe_msg))
        response = self.req_socket.recv_string()
        response_data = json.loads(response)
        
        if response_data.get("status") == "success":
            self.logger.info(f"Successfully unsubscribed from: {event_types}")
            
            # Unsubscribe from event types on SUB socket
            for event_type in event_types:
                self.sub_socket.setsockopt_string(zmq.UNSUBSCRIBE, event_type)
        else:
            self.logger.error(f"Failed to unsubscribe: {response_data.get('message')}")
    
    def send_heartbeat(self):
        """Send heartbeat to server"""
        heartbeat_msg = {
            "type": "heartbeat",
            "client_id": self.client_id
        }
        
        self.req_socket.send_string(json.dumps(heartbeat_msg))
        response = self.req_socket.recv_string()
        response_data = json.loads(response)
        
        if response_data.get("status") != "success":
            self.logger.warning(f"Heartbeat failed: {response_data.get('message')}")
    
    def on_event(self, event_type: str):
        """Decorator for registering event handlers"""
        def decorator(handler):
            self.event_handlers[event_type].append(handler)
            return handler
        return decorator
    
    def _event_listener(self):
        """Background thread for listening to events"""
        while self.running:
            try:
                # Receive event message
                message = self.sub_socket.recv_string()
                
                # Parse event
                event_data = json.loads(message)
                event_type = event_data.get("type")
                
                # Call all registered handlers for this event type
                for handler in self.event_handlers.get(event_type, []):
                    try:
                        handler(event_data)
                    except Exception as e:
                        self.logger.error(f"Error in event handler: {e}")
                
            except Exception as e:
                if self.running:  # Only log if we're still running
                    self.logger.error(f"Error in event listener: {e}")
    
    def start_listening(self):
        """Start listening for events"""
        if self.running:
            self.logger.warning("Already listening for events")
            return
        
        self.running = True
        self.listener_thread = threading.Thread(target=self._event_listener, daemon=True)
        self.listener_thread.start()
        self.logger.info("Started listening for events")
    
    def stop_listening(self):
        """Stop listening for events"""
        self.running = False
        if self.listener_thread:
            self.listener_thread.join(timeout=1.0)
        self.logger.info("Stopped listening for events")
    
    def close(self):
        """Close connections and cleanup"""
        self.stop_listening()
        
        try:
            self.req_socket.close()
        except Exception as e:
            self.logger.error(f"Error closing REQ socket: {e}")
        
        try:
            self.sub_socket.close()
        except Exception as e:
            self.logger.error(f"Error closing SUB socket: {e}")
        
        try:
            self.context.term()
        except Exception as e:
            self.logger.error(f"Error terminating context: {e}")
        
        self.logger.info("ZeroMQ signaling client closed")