#!/bin/bash



# Remove the existing 'signaling' container if it is already running or exists
if [ "$(docker ps -aq -f name=^lunaricorn_signaling$)" ]; then
    echo "Removing existing 'lunaricorn_signaling' container..."
    docker rm -f lunaricorn_signaling
fi
./build.sh
# Create the tmp/data directory if it doesn't exist
docker run -it --name lunaricorn_signaling -p 8002:8000 -v "$(pwd)/cfg:/opt/lunaricorn/signaling_data" lunaricorn_signaling