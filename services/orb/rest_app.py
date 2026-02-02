
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

app = Flask(__name__)
storage = None

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

@app.route('/health', methods=['GET'])
def health_check():
    try:
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

@app.route('/', methods=['GET'])
def root():
    """Root endpoint"""
    return jsonify({
        'message': 'Orb Service',
        'status': 'running'
    })


@app.errorhandler(404)
def not_found(error):
    return jsonify({'error': 'Not found'}), 404

@app.errorhandler(500)
def internal_error(error):
    return jsonify({'error': 'Internal server error'}), 500

service_running = True

def signal_handler(sig, frame):
    """Handle shutdown signals"""
    global service_running
    logger.info('Received shutdown signal')
    service_running = False
    sys.exit(0)

signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)

def create_app(orb_storage):
    global storage
    storage = orb_storage
    return app
