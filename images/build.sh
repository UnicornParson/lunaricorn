#!/bin/bash

mkdir -p tmp
IMAGE_NAME=CPP_BUILDER_BASE
set -e
docker build --no-cache --progress=plain -t $IMAGE_NAME -f Dockerfile.base . 2>&1 | tee -i tmp/IMAGE_NAME.log

if [ $? -eq 0 ]; then
    echo "IMAGE_NAME build completed successfully!"
else
    echo "Build failed! Check tmp/IMAGE_NAME.log for details."
    exit 1
fi