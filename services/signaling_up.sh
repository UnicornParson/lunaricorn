#!/bin/bash
set -e

if [ ! -f ".env" ]; then
    echo "Error: .env file not found"
    exit 1
fi

echo "Building signaling service..."
pushd signaling > /dev/null
./build.sh
popd > /dev/null

docker compose stop signaling && docker compose rm -f signaling
docker compose up -d --build --force-recreate signaling

echo "Ыignaling service restarted"