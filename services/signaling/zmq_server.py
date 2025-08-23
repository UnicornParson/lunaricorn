import zmq
import json
import threading
import time
import logging
import os
import sys
from typing import Dict, Any
from enum import Enum

from internal import *

class ZeroMQSignalingServer:
    """Simplified ZeroMQ signaling server for node communication"""
    
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
        self.heartbeat_interval = config.get("zmq", {}).get("heartbeat_interval", 30)
        
        # Initialize signaling service
        self.signaling = Signaling(config)
        
        # Server state
        self.running = False
        
        # Initialize socket
        self._setup_socket()
    
    def _setup_socket(self):
        """Setup ZeroMQ REP socket for request-response pattern"""
        try:
            # REP socket for handling requests
            self.socket = self.context.socket(zmq.REP)
            address = f"{self.protocol}://{self.bind_address}:{self.port}"
            self.socket.bind(address)
            self.logger.info(f"REP socket bound to {address}")
            
        except Exception as e:
            self.logger.error(f"Failed to setup socket: {e}")
            raise
    
    def _process_message(self, message_data: Dict[str, Any]) -> Dict[str, Any]:
        """Process incoming message and return response"""
        try:
            msg_type = message_data.get("type")
            
            # Handle supported message types
            if msg_type == SignalingMessageType.PUSH_EVENT.value:
                return self.signaling.handle_push_event(message_data)
            
            elif msg_type == SignalingMessageType.SUBSCRIBE.value:
                return self.signaling.handle_subscribe(message_data)
            
            else:
                return {
                    "status": "error", 
                    "message": f"Unsupported message type: {msg_type}",
                    "supported_types": [t.value for t in SignalingMessageType]
                }
                
        except Exception as e:
            self.logger.error(f"Error processing message: {e}")
            return {"status": "error", "message": f"Processing error: {e}"}
    
    def _cleanup_worker(self):
        """Background worker for cleanup tasks"""
        while self.running:
            try:
                self.signaling.cleanup_dead_subscribers()
                time.sleep(self.heartbeat_interval)
            except Exception as e:
                self.logger.error(f"Error in cleanup worker: {e}")
    
    def start(self):
        """Start the ZeroMQ signaling server"""
        self.running = True
        self.logger.info("Starting simplified ZeroMQ signaling server...")
        
        # Start cleanup worker
        cleanup_thread = threading.Thread(target=self._cleanup_worker, daemon=True)
        cleanup_thread.start()
        
        # Main server loop
        try:
            while self.running:
                try:
                    # Wait for request (blocking)
                    message_raw = self.socket.recv_string(zmq.NOBLOCK)
                    
                    # Parse message
                    try:
                        message_data = json.loads(message_raw)
                    except json.JSONDecodeError as e:
                        response = {"status": "error", "message": f"Invalid JSON: {e}"}
                    else:
                        response = self._process_message(message_data)
                    
                    # Send response
                    self.socket.send_string(json.dumps(response))
                    
                except zmq.Again:
                    # No message available, continue
                    time.sleep(0.01)
                    continue
                    
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
        
        # Close socket
        try:
            self.socket.close()
        except Exception as e:
            self.logger.error(f"Error closing socket: {e}")
        
        # Shutdown signaling service
        try:
            self.signaling.shutdown()
        except Exception as e:
            self.logger.error(f"Error shutting down signaling service: {e}")
        
        # Terminate context
        try:
            self.context.term()
        except Exception as e:
            self.logger.error(f"Error terminating context: {e}")
        
        self.logger.info("ZeroMQ signaling server stopped") 