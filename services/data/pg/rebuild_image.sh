#!/bin/bash

# Set error handling
set -e

echo "Starting Docker build for PostgreSQL image..."

# Build the Docker image with detailed output
docker build --no-cache --progress=plain -t lunaricorn-pg . 2>&1 | tee -i build.log

# Check if build was successful
if [ $? -eq 0 ]; then
    echo "Build completed successfully!"
    echo "Image: lunaricorn-pg"
else
    echo "Build failed! Check tmp/build.log for details."
    exit 1
fi