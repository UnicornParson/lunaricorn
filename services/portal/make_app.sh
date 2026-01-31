#!/bin/bash
mkdir -p tmp
set -e
IMG_NAME=lunaricorn_portal
BASE_IMG=lunaricorn_portal_base

if ! docker image inspect "$BASE_IMG" &>/dev/null; then
    echo "Base image $BASE_IMG not found. Building it first..."
    "$(dirname "$0")/make_base.sh"
fi

rm -rvf tmp/lunaricorn.tgz
tar -cvzf tmp/lunaricorn.tgz ../../lunaricorn

docker build --no-cache --progress=plain -t $IMG_NAME -f Dockerfile .

# Check if build was successful
if [ $? -eq 0 ]; then
    echo "Build completed successfully!"
    echo "Image: $IMG_NAME"
else
    echo "Build failed! Check tmp/build_app.log for details."
    exit 1
fi
