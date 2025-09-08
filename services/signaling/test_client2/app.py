import uuid
import time
import logging
from signaling_client import *
# Configure logging
logging.basicConfig(level=logging.INFO)

def event_handler(event_data):
    print(f"Received event: {event_data}")

config = SignalingClientConfig (
    host = "localhost",
    rep_port = 5555,
    pub_port = 5556
)

client = SignalingClient(config, "test_client")
if not client.connect():
    print("Failed to connect to server")
    exit(1)

# Event handler function
def handle_user_event(event: ClientEventData):
    print(f"Received event: {event}")


client.set_event_callback(handle_user_event)
client.subscribe(["user_created", "user_updated", "order_created", "test_event"])
try:
    response = client.subscribe(["user_created", "user_updated", "order_created"])
    print(f"Subscription response: {response}")
except Exception as e:
    print(f"Subscription failed: {e}")



for i in range(10):
    responce = client.push_event(
        event_type="test_event",
        payload={"message": "Hello, World!", "value": i, "u": str(uuid.uuid4())},
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