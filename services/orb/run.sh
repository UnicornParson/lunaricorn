#!/bin/bash
# Remove the existing 'orb' container if it is already running or exists
if [ "$(docker ps -aq -f name=^orb$)" ]; then
    echo "Removing existing 'orb' container..."
    docker rm -f orb
fi

# Create the tmp/data directory if it doesn't exist
mkdir -p tmp/data
docker run -it --name orb -p 8001:8080 -v "$(pwd)/tmp/data:/opt/lunaricorn/orb_data" -v "$(pwd)/tmp/file_storage:/opt/lunaricorn/orb/file_storage" lunaricorn/orb