import zmq
import json
import threading
import time
import logging
import os
import sys
from typing import Dict, Any
from enum import Enum
from collections import defaultdict
from dataclasses import dataclass

from internal import *

class ZeroMQPattern(Enum):
    PUB_SUB = "pub_sub"
    PUSH_PULL = "push_pull"
    ROUTER_DEALER = "router_dealer"
    REQ_REP = "req_rep"

class ZeroMQSignalingServer:
    """Simplified ZeroMQ signaling server for node communication"""
    
    def __init__(self, config: Dict[str, Any]):
        self.config = config
        self.logger = logging.getLogger(__name__)
        
        # ZeroMQ context
        self.context = zmq.Context()
        
        # Server configuration
        self.host = config.get("host", "0.0.0.0")
        self.rep_port = config.get("rep_port", 5555)  # REQ-REP port
        self.pub_port = config.get("pub_port", 5556)  # PUB-SUB port
        self.protocol = config.get("zmq", {}).get("protocol", "tcp")
        self.bind_address = config.get("zmq", {}).get("bind_address", "*")
        self.heartbeat_interval = config.get("zmq", {}).get("heartbeat_interval", 30)
        
        # Initialize signaling service
        self.signaling = Signaling(config, self.publish_event)
        
        # Server state
        self.running = False
        
        # Initialize sockets
        self.rep_socket = None  # REQ-REP socket
        self.pub_socket = None  # PUB socket
        self._setup_sockets()

        # Client management
        self.subscriptions = defaultdict(set)

        self.threads = []
    
    def _setup_sockets(self):
        """Setup ZeroMQ sockets"""
        try:
            # REP socket for request-response pattern
            self.rep_socket = self.context.socket(zmq.REP)
            self.rep_socket.bind(f"tcp://*:{self.rep_port}")
            self.logger.info(f"REP socket bound to port {self.rep_port}")
            
            # PUB socket for publish-subscribe pattern
            self.pub_socket = self.context.socket(zmq.PUB)
            self.pub_socket.bind(f"tcp://*:{self.pub_port}")
            self.logger.info(f"PUB socket bound to port {self.pub_port}")
            
        except Exception as e:
            self.logger.error(f"Failed to setup sockets: {e}")
            raise
    def _process_request(self, message_data: Dict[str, Any]) -> Dict[str, Any]:
        """Process incoming request and return response"""
        try:
            msg_type = message_data.get("type")
            client_id = message_data.get("client_id")
            
            if not client_id:
                return {"status": "error", "message": "client_id is required"}
            
            # Update client activity
            self.signaling.on_client_action(client_id)

            if msg_type == "heartbeat":
                # Handle heartbeat
                self.logger.debug(f"Heartbeat from client {client_id}")
                return {"status": "success", "message": "Heartbeat received"}
            elif msg_type == "push":
                self.logger.info("on push")
                required_fields = ["type", "message"]
                for field in required_fields:
                    if field not in message_data:
                        self.logger.error(f"Missing required field: {field}")
                        return {"status": "error", "message": f"Missing required field: {field}"}

                event_data = EventData(
                    event_type=message_data["type"],
                    payload=message_data["message"],
                    timestamp=message_data.get("timestamp", time.time()),
                    source=message_data.get("creator-id"),
                    affected=None,
                    tags=message_data.get("tags")
                )
                
                
                eid = self.signaling.push(event_data)
                return {"status": "success", "eid": eid}
            
            else:
                self.logger.info(f"unknown event {msg_type}")
                return {
                    "status": "error", 
                    "message": f"Unsupported message type: {msg_type}",
                    "supported_types": ["subscribe", "unsubscribe", "heartbeat"]
                }
                
        except Exception as e:
            self.logger.error(f"Error processing request: {e}")
            return {"status": "error", "message": f"Processing error: {e}"}
        
    def publish_event(self, event_data: EventDataExtended):
        """Publish an event to all subscribed clients"""
        print("try publish_event")
        try:
            # Create event message
            event_message = {
                "eid": event_data.eid,
                "type": event_data.event_type,
                "payload": event_data.payload,
                "timestamp": event_data.timestamp,
                "source": event_data.source,
                "affected": event_data.affected,
                "tags": event_data.tags
            }
            
            # Publish event to all subscribers
            self.pub_socket.send_string(json.dumps(event_message))
            self.logger.debug(f"Published event: {event_data.event_type} (ID: {event_data.eid})")
            
        except Exception as e:
            self.logger.error(f"Error publishing event: {e}")
    

    
    def start(self):
        """Start the ZeroMQ signaling server"""
        self.running = True
        self.logger.info("Starting ZeroMQ signaling server...")

        # Main server loop for REQ-REP
        try:
            while self.running:
                try:
                    # Wait for request
                    message_raw = self.rep_socket.recv_string()

                    try:
                        message_data = json.loads(message_raw)
                    except json.JSONDecodeError as e:
                        response = {"status": "error", "message": f"Invalid JSON: {e}"}
                    else:
                        response = self._process_request(message_data)
                    
                    # Send response
                    self.rep_socket.send_string(json.dumps(response))
                    
                except Exception as e:
                    self.logger.error(f"Unexpected error in main loop: {e}")
                    time.sleep(0.1)  # Prevent busy waiting on error
                    
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
        try:
            if self.rep_socket:
                self.rep_socket.close()
        except Exception as e:
            self.logger.error(f"Error closing REP socket: {e}")
        
        try:
            if self.pub_socket:
                self.pub_socket.close()
        except Exception as e:
            self.logger.error(f"Error closing PUB socket: {e}")
        
        # Terminate context
        try:
            self.context.term()
        except Exception as e:
            self.logger.error(f"Error terminating context: {e}")
        
        self.logger.info("ZeroMQ signaling server stopped")