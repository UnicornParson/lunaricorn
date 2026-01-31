#!/bin/bash
set -e

if [ ! -f ".env" ]; then
    echo "Error: .env file not found"
    exit 1
fi

echo "Building leader service..."
pushd leader > /dev/null
./make_app.sh
popd > /dev/null

docker compose stop leader && docker compose rm -f leader
docker compose up -d --build --force-recreate leader

echo "Leader service restarted"