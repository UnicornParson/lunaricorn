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

BUILD=false

while getopts "b" opt; do
  case "$opt" in
    b)
      BUILD=true
      ;;
  esac
done

# Exit immediately if a command exits with a non-zero status
set -e
docker compose down -v
if [ "$BUILD" = true ]; then
    echo "Running build.sh..."
    ./build.sh
else
    echo "Skipping build.sh (use -b to enable)"
fi

# Print info message
echo "Starting services using docker-compose.yaml..."

# Run docker-compose up in detached mode
docker compose -f docker-compose.yaml up -d --build --force-recreate

# Print status of the containers
echo "Current status of containers:"
docker compose -f docker-compose.yaml ps
