#!/bin/bash



# Remove the existing 'portal' container if it is already running or exists
if [ "$(docker ps -aq -f name=^lunaricorn_portal$)" ]; then
    echo "Removing existing 'lunaricorn_portal' container..."
    docker rm -f lunaricorn_portal
fi
./build.sh
# Create the tmp/data directory if it doesn't exist
docker run -it --name lunaricorn_portal -p 8002:8000 -v "$(pwd)/cfg:/opt/lunaricorn/portal/data" lunaricorn_portal