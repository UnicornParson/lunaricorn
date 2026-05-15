#!/bin/bash
mkdir -p tmp
set -e
IMAGE_NAME=lunaricorn_signaling_test
BASE_IMG=lunaricorn_signaling_cpp_base
BUILDER_TAG=lunaricorn_signaling_cpp_builder
TOKEN=$(date +%s)
if ! docker image inspect "$BASE_IMG" &>/dev/null; then
    echo "Base image $BASE_IMG not found. Building it first..."
    "$(dirname "$0")/make_base.sh"
fi
if ! docker image inspect "$BUILDER_TAG" &>/dev/null; then
    echo "Base image $BUILDER_TAG not found. Building it first..."
    "$(dirname "$0")/make_base.sh"
fi
rm -rvf tmp/lunaricorn.tgz
tar -cvzf tmp/lunaricorn.tgz ../../lunaricorn/cpp
docker build --no-cache --progress=plain -t $IMAGE_NAME -f Dockerfile.tester . 2>&1 | tee -i tmp/build_test.$TOKEN.log

# Check if build was successful
if [ $? -eq 0 ]; then
    echo "Build completed successfully!"
    echo "Image: $IMAGE_NAME"
else
    echo "Build failed! Check tmp/build_app.log for details."
    exit 1
fi



NETWORK_NAME="lunaricorn-network"
CONTEXT_DIR=$(pwd)
SIGNALING_HOST="192.168.0.18"
MAINTENANCE_API_PORT=8007
docker run -it --rm \
    --name "signaling-test" \
    -e MAINTENANCE_HOST="192.168.0.18" \
    -e MAINTENANCE_PORT=${MAINTENANCE_API_PORT} \
    -e SIGNALING_REQ=5555 \
    -e SIGNALING_PUB=5556 \
    -e SIGNALING_API=5557 \
    -e SIGNALING_RAW=5558 \
    "$IMAGE_NAME"