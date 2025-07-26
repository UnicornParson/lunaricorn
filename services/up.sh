#!/bin/bash

if [ ! -f ".env" ]; then
    echo "Error: .env file not found"
    exit 1
fi

source .env
mkdir -p ${LUNARICORN_HOME}
mkdir -p ${LUNARICORN_HOME}/leader_data
mkdir -p ${LUNARICORN_HOME}/portal_data
mkdir -p ${LUNARICORN_HOME}/pg_data

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
