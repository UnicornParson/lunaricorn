#!/bin/bash
set -e

if [ ! -f ".env" ]; then
    echo "Error: .env file not found"
    exit 1
fi

echo "Building portal service..."
pushd portal > /dev/null
./build.sh
popd > /dev/null

docker compose stop portal && docker compose rm -f portal
docker compose up -d --build --force-recreate portal

echo "Portal service restarted"