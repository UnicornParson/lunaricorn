#!/bin/bash
set -e

if [ ! -f ".env" ]; then
    echo "Error: .env file not found"
    exit 1
fi

#source .env

docker compose stop portal && docker compose rm -f portal
docker compose up -d --build --force-recreate portal

echo "Portal service restarted"