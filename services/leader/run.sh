#!/bin/bash
# Remove the existing 'leader' container if it is already running or exists
if [ "$(docker ps -aq -f name=^leader$)" ]; then
    echo "Removing existing 'leader' container..."
    docker rm -f leader
fi

# Create the tmp/data directory if it doesn't exist
mkdir -p tmp/data
docker run -it --name leader -p 8001:8000 -v "$(pwd)/tmp/data:/opt/lunaricorn/leader_data" lunaricorn/leader