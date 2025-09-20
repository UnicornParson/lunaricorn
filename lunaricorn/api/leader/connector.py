import requests
import json
import logging
import threading
from typing import Dict, List, Optional, Any
from datetime import datetime
import time

logger = logging.getLogger(__name__)

class LeaderConnector:
    """
    Wrapper class for interacting with the Leader API.
    Provides methods to communicate with the leader service for service discovery and health monitoring.
    """
    
    def __init__(self, base_url: str = "http://localhost:8001", timeout: int = 30):
        """
        Initialize the connector with the leader API base URL.
        
        Args:
            base_url: Base URL of the leader API (default: http://localhost:8001)
            timeout: Request timeout in seconds (default: 30)
        """
        self.base_url = base_url.rstrip('/')
        self.timeout = timeout
        self.session = requests.Session()
        self.session.headers.update({
            'Content-Type': 'application/json',
            'Accept': 'application/json'
        })
        
        # Service registration timer variables
        self._registration_timer = None
        self._registration_stop_event = threading.Event()
        self._registered_service = None
        
        logger.info(f"LeaderConnector initialized with base URL: {self.base_url}")
    
    def _make_request(self, method: str, endpoint: str, data: Optional[Dict] = None) -> Dict:
        """
        Make HTTP request to the leader API.
        
        Args:
            method: HTTP method (GET, POST, etc.)
            endpoint: API endpoint path
            data: Request data for POST requests
            
        Returns:
            Response data as dictionary
            
        Raises:
            requests.RequestException: If request fails
            ValueError: If response is not valid JSON
        """
        url = f"{self.base_url}{endpoint}"
        
        try:
            if method.upper() == 'GET':
                response = self.session.get(url, timeout=self.timeout)
            elif method.upper() == 'POST':
                response = self.session.post(url, json=data, timeout=self.timeout)
            else:
                raise ValueError(f"Unsupported HTTP method: {method}")
            
            response.raise_for_status()
            
            # Handle empty responses
            if response.text.strip():
                return response.json()
            else:
                return {}
                
        except requests.exceptions.RequestException as e:
            logger.error(f"Request failed for {method} {url}: {e}")
            raise
        except json.JSONDecodeError as e:
            logger.error(f"Invalid JSON response from {url}: {e}")
            raise ValueError(f"Invalid JSON response: {e}")
    
    def health_check(self) -> Dict:
        """
        Check if the leader API is healthy.
        
        Returns:
            Health status information
        """
        try:
            return self._make_request('GET', '/health')
        except Exception as e:
            logger.error(f"Health check failed: {e}")
            raise
    
    def get_api_info(self) -> Dict:
        """
        Get API information from the root endpoint.
        
        Returns:
            API information
        """
        try:
            return self._make_request('GET', '/')
        except Exception as e:
            logger.error(f"Failed to get API info: {e}")
            raise
    
    def _send_registration_request(self) -> bool:
        """
        Send registration request to the leader API.
        This function is called by the timer every second.
        
        Returns:
            True if successful, False otherwise
        """
        if not self._registered_service:
            logger.warning("No service registered for periodic updates")
            return False
        
        try:
            data: Dict[str, Any] = {
                'node_name': self._registered_service['node_name'],
                'node_type': self._registered_service['node_type'],
                'instance_key': self._registered_service['instance_key']
            }
            
            if self._registered_service.get('host') is not None:
                data['host'] = self._registered_service['host']
            if self._registered_service.get('port') is not None:
                data['port'] = self._registered_service['port']
            if self._registered_service.get('additional') is not None:
                data['additional'] = self._registered_service['additional']
            
            response = self._make_request('POST', '/v1/imalive', data)
            logger.debug(f"Periodic registration successful: {self._registered_service['node_name']}")
            return True
            
        except Exception as e:
            logger.error(f"Periodic registration failed for {self._registered_service.get('node_name', 'unknown')}: {e}")
            return False
    
    def _registration_timer_worker(self):
        """
        Timer worker function that sends registration requests every second.
        """
        while not self._registration_stop_event.is_set():
            self._send_registration_request()
            # Wait for 1 second or until stop event is set
            if self._registration_stop_event.wait(1.0):
                break
        
        logger.info("Registration timer stopped")
    
    def register_service(self, 
                        node_name: str, 
                        node_type: str, 
                        instance_key: str,
                        host: Optional[str] = None,
                        port: Optional[int] = None,
                        additional: Optional[Dict[str, Any]] = None) -> Dict:
        """
        Register a service with the leader API and start periodic registration timer.
        
        Args:
            node_name: Name of the service node
            node_type: Type of the service node
            instance_key: Unique instance identifier
            host: Host address (optional)
            port: Port number (optional)
            additional: Additional JSON data (optional)
            
        Returns:
            Registration response
        """
        # Stop any existing timer
        self.stop_registration_timer()
        
        # Save service parameters as class variables
        self._registered_service = {
            'node_name': node_name,
            'node_type': node_type,
            'instance_key': instance_key,
            'host': host,
            'port': port,
            'additional': additional
        }
        
        # Send initial registration
        data: Dict[str, Any] = {
            'node_name': node_name,
            'node_type': node_type,
            'instance_key': instance_key
        }
        
        if host is not None:
            data['host'] = host
        if port is not None:
            data['port'] = port
        if additional is not None:
            data['additional'] = additional
        
        try:
            logger.info(f"Registering service: {node_name} ({node_type})")
            response = self._make_request('POST', '/v1/imalive', data)
            
            # Start periodic registration timer
            self._registration_stop_event.clear()
            self._registration_timer = threading.Thread(
                target=self._registration_timer_worker,
                daemon=True
            )
            self._registration_timer.start()
            logger.info(f"Started periodic registration timer for {node_name}")
            
            return response
            
        except Exception as e:
            logger.error(f"Failed to register service {node_name}: {e}")
            self._registered_service = None
            raise
    
    def stop_registration_timer(self):
        """
        Stop the periodic registration timer.
        """
        if self._registration_timer and self._registration_timer.is_alive():
            logger.info("Stopping registration timer")
            self._registration_stop_event.set()
            self._registration_timer.join(timeout=2.0)  # Wait up to 2 seconds for thread to stop
            self._registration_timer = None
            self._registered_service = None
            logger.info("Registration timer stopped")
        elif self._registration_timer:
            logger.debug("Registration timer was already stopped")
            self._registration_timer = None
            self._registered_service = None
    
    def list_services(self) -> Dict:
        """
        Get list of all registered services.
        
        Returns:
            List of services with metadata
        """
        try:
            return self._make_request('GET', '/v1/list')
        except Exception as e:
            logger.error(f"Failed to list services: {e}")
            raise
    
    def discover_services(self, query: str) -> Dict:
        """
        Discover services based on query.
        
        Args:
            query: Discovery query string
            
        Returns:
            Discovery results
        """
        data = {'query': query}
        
        try:
            logger.info(f"Discovering services with query: {query}")
            return self._make_request('POST', '/v1/discover', data)
        except Exception as e:
            logger.error(f"Failed to discover services with query '{query}': {e}")
            raise
    
    def get_environment(self) -> Dict:
        """
        Get environment information and cluster configuration.
        
        Returns:
            Environment and cluster configuration
        """
        try:
            return self._make_request('GET', '/v1/getenv')
        except Exception as e:
            logger.error(f"Failed to get environment: {e}")
            raise
    
    def get_cluster_info(self) -> Dict:
        """
        Get detailed cluster information.
        
        Returns:
            Detailed cluster status and information
        """
        try:
            return self._make_request('GET', '/v1/clusterinfo')
        except Exception as e:
            logger.error(f"Failed to get cluster info: {e}")
            raise
    
    def is_ready(self) -> bool:
        """
        Check if the leader service is ready to handle requests.
        
        Returns:
            True if ready, False otherwise
        """
        try:
            health_data = self.health_check()
            return health_data.get('status') == 'healthy'
        except Exception:
            return False
    
    def wait_for_ready(self, timeout: int = 60, check_interval: int = 5) -> bool:
        """
        Wait for the leader service to become ready.
        
        Args:
            timeout: Maximum time to wait in seconds
            check_interval: Time between checks in seconds
            
        Returns:
            True if service became ready, False if timeout exceeded
        """
        start_time = time.time()
        
        while time.time() - start_time < timeout:
            if self.is_ready():
                logger.info("Leader service is ready")
                return True
            
            logger.debug(f"Leader service not ready, waiting {check_interval} seconds...")
            time.sleep(check_interval)
        
        logger.warning(f"Leader service did not become ready within {timeout} seconds")
        return False
    
    def get_service_by_name(self, service_name: str) -> Optional[Dict]:
        """
        Get information about a specific service by name.
        
        Args:
            service_name: Name of the service to find
            
        Returns:
            Service information if found, None otherwise
        """
        try:
            services_data = self.list_services()
            services = services_data.get('services', [])
            
            for service in services:
                if service.get('name') == service_name:
                    return service
            
            return None
        except Exception as e:
            logger.error(f"Failed to get service {service_name}: {e}")
            return None
    
    def get_services_by_type(self, service_type: str) -> List[Dict]:
        """
        Get all services of a specific type.
        
        Args:
            service_type: Type of services to find
            
        Returns:
            List of services of the specified type
        """
        try:
            services_data = self.list_services()
            services = services_data.get('services', [])
            
            return [service for service in services if service.get('type') == service_type]
        except Exception as e:
            logger.error(f"Failed to get services of type {service_type}: {e}")
            return []
    
    def ping_service(self, node_name: str, node_type: str, instance_key: str) -> bool:
        """
        Ping a service by registering it with the leader.
        
        Args:
            node_name: Name of the service
            node_type: Type of the service
            instance_key: Instance identifier
            
        Returns:
            True if ping successful, False otherwise
        """
        try:
            response = self.register_service(node_name, node_type, instance_key)
            return response.get('status') == 'received'
        except Exception as e:
            logger.error(f"Failed to ping service {node_name}: {e}")
            return False
    
    def close(self):
        """
        Close the connector and clean up resources.
        """
        # Stop registration timer
        self.stop_registration_timer()
        
        # Close session
        if hasattr(self, 'session'):
            self.session.close()
        logger.info("LeaderConnector closed")

    def get_next_message_id(self) -> int:
        """
        Get the next message id.
        """
        try:
            return self._make_request('GET', '/v1/utils/get_mid')
        except Exception as e:
            logger.error(f"Failed to get next message id: {e}")
            return 0

    def get_next_object_id(self) -> int:

        """
        Get the next object id.
        """
        try:
            return self._make_request('GET', '/v1/utils/get_oid')
        except Exception as e:
            logger.error(f"Failed to get next object id: {e}")
            raise

    def get_cluster_info(self) -> Dict:
        """
        Get the cluster info.
        """
        try:
            response = self._make_request('GET', '/v1/clusterinfo')
            # Ensure the result is in the required format
            if (
                isinstance(response, dict)
                and "nodes_summary" in response
                and "required_nodes" in response
            ):
                return response
            nodes_summary = response.get("nodes_summary", {})
            required_nodes = response.get("required_nodes", [])
            if not nodes_summary and "nodes" in response:
                nodes_summary = response["nodes"]
            if not required_nodes and "required" in response:
                required_nodes = response["required"]
            return {
                "nodes_summary": nodes_summary,
                "required_nodes": required_nodes
            }
        except Exception as e:
            logger.error(f"Failed to get cluster info: {e}")
            raise e


            
class ConnectorUtils:
    @staticmethod
    def create_leader_connector(base_url: str = "http://localhost:8001") -> LeaderConnector:
        return LeaderConnector(base_url)

    @staticmethod
    def quick_health_check(base_url: str = "http://localhost:8001") -> bool:
        try:
            connector = LeaderConnector(base_url)
            return connector.is_ready()
        except Exception:
            return False

    @staticmethod
    def test_connection(base_url: str = "http://localhost:8001") -> bool:
        try:
            if not base_url:
                return False
            # Try to send a GET request to the base_url using requests
            response = requests.get(base_url, timeout=5)
            # Consider connection successful if status code is 200
            return response.status_code == 200
        except Exception as e:
            # Log the exception if needed
            logger.error(f"Failed to connect to {base_url}: {e}")
            return False

    @staticmethod
    def wait_connection(url: str, seconds_timeout: float) -> bool:
        start_time = time.time()
        while True:
            if ConnectorUtils.test_connection(url):
                return True
            if time.time() - start_time >= seconds_timeout:
                return False
            time.sleep(0.5)