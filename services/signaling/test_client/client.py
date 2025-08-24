from .ZeroMQSignalingClient import *


config = {
    "host": "0.0.0.0",
    "rep_port": 5555,
    "pub_port": 5556,
    "heartbeat_interval": 30
}


# Client setup
client = ZeroMQSignalingClient("localhost", 5555, 5556)

# Subscribe to events
client.subscribe(["user_connected", "message_received"])

# Register event handlers
@client.on_event("user_connected")
def handle_user_connected(event_data):
    print(f"User connected: {event_data.get('payload', {}).get('user_id')}")

@client.on_event("message_received")
def handle_message_received(event_data):
    print(f"Message received: {event_data.get('payload', {}).get('message')}")

# Start listening for events
client.start_listening()

# Send heartbeat periodically
import time
try:
    while True:
        client.send_heartbeat()
        time.sleep(30)
except KeyboardInterrupt:
    pass
finally:
    client.close()
