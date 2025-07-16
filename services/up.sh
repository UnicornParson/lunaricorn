#!/bin/bash
# This script starts the services defined in docker-compose.yaml

# Exit immediately if a command exits with a non-zero status
set -e
docker compose down -v
./build.sh

# Print info message
echo "Starting services using docker-compose.yaml..."

# Run docker-compose up in detached mode
docker compose -f docker-compose.yaml up -d --build --force-recreate

# Print status of the containers
echo "Current status of containers:"
docker compose -f docker-compose.yaml ps
