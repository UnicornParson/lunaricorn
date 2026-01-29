#!/bin/bash
mkdir -p tmp
set -e
IMG_NAME=lunaricorn_maintenance
BASE_IMG=lunaricorn_maintenance_base

if ! docker image inspect "$BASE_IMG" &>/dev/null; then
    echo "Base image $BASE_IMG not found. Building it first..."
    "$(dirname "$0")/make_base.sh"
fi

docker build --no-cache --progress=plain -t $IMG_NAME -f Dockerfile . 2>&1 | tee -i tmp/build_app.log

# Check if build was successful
if [ $? -eq 0 ]; then
    echo "Build completed successfully!"
    echo "Image: $IMG_NAME"
else
    echo "Build failed! Check tmp/build_app.log for details."
    exit 1
fi
