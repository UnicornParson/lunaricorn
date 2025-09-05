import json
import time
import logging
from signaling_client import *
# Configure logging
logging.basicConfig(level=logging.INFO)

def event_handler(event_data):
    print(f"Received event: {event_data}")

# Client configuration
config = {
    "server_host": "localhost",
    "rep_port": 5555,
    "pub_port": 5556,
    "protocol": "tcp",
    "heartbeat_interval": 30
}

# Create and connect client
client = SignalingClient(config)
if not client.connect():
    print("Failed to connect to server")
    exit(1)

# Event handler function
def handle_user_event(event):
    print(f"Received event: {event['type']}")
    print(f"Payload: {json.dumps(event.get('payload', {}), indent=2)}")
    print(f"Timestamp: {event.get('timestamp')}")
    print("---")

# Register event handlers
client.add_event_handler("user_created", handle_user_event)
client.add_event_handler("user_updated", handle_user_event)
client.add_event_handler("order_created", handle_user_event)

# Subscribe to events
try:
    response = client.subscribe(["user_created", "user_updated", "order_created"])
    print(f"Subscription response: {response}")
except Exception as e:
    print(f"Subscription failed: {e}")

# Send heartbeat
try:
    response = client.send_heartbeat()
    print(f"Heartbeat response: {response}")
except Exception as e:
    print(f"Heartbeat failed: {e}")


client.subscribe(["*"])

# Регистрация обработчика событий
client.add_event_handler("*", event_handler)


response = client.push_event(
    event_type="test_event",
    payload={"message": "Hello, World!", "value": 42},
    source="test_client",
    tags=["test", "example"]
)

try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    print("Shutting down...")
finally:
    # Cleanup
    try:
        client.unsubscribe(["user_created", "user_updated", "order_created"])
    except:
        pass
    client.disconnect()