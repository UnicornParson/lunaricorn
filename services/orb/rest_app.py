
from flask import Flask, jsonify
import logging
import yaml
import threading
import time
import signal
import sys
import os

# Import lunaricorn modules
from lunaricorn.api.leader import ConnectorUtils as leader
from lunaricorn.api.signaling import SignalingClient as signaling

# Create Flask app
app = Flask(__name__)

# Configure logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# Health check endpoint
@app.route('/health', methods=['GET'])
def health_check():
    """Health check endpoint"""
    try:
        # Simple health check - just return that the service is running
        return jsonify({
            'status': 'healthy',
            'timestamp': time.time(),
            'service': 'orb'
        }), 200
    except Exception as e:
        logger.error(f"Health check failed: {e}")
        return jsonify({
            'status': 'unhealthy',
            'error': str(e)
        }), 500

# Additional endpoints can be added here
@app.route('/', methods=['GET'])
def root():
    """Root endpoint"""
    return jsonify({
        'message': 'Orb Service',
        'status': 'running'
    })

# Error handlers
@app.errorhandler(404)
def not_found(error):
    return jsonify({'error': 'Not found'}), 404

@app.errorhandler(500)
def internal_error(error):
    return jsonify({'error': 'Internal server error'}), 500

# Global variables for tracking service state
service_running = True

def signal_handler(sig, frame):
    """Handle shutdown signals"""
    global service_running
    logger.info('Received shutdown signal')
    service_running = False
    sys.exit(0)

# Register signal handlers
signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)

# Export the app for use in main.py
def create_app():
    """Factory function to create the Flask app"""
    return app

if __name__ == '__main__':
    # This will only run when directly executing this file
    logger.info("Starting Orb Service with Flask")
    app.run(host='0.0.0.0', port=8080, debug=False)
