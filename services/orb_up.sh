#!/bin/bash
set -e

if [ ! -f ".env" ]; then
    echo "Error: .env file not found"
    exit 1
fi

echo "Building orb service..."
pushd orb > /dev/null
./make_app.sh
popd > /dev/null

docker compose stop orb && docker compose rm -f orb
docker compose up -d --build --force-recreate orb

echo "Orb service restarted"