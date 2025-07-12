#!/bin/bash

# Create tmp directory if it doesn't exist
mkdir -p tmp

# Set error handling
set -e
    
echo "Starting Docker build for lunaricorn/leader..."

# Build the Docker image with detailed output
docker build --progress=plain -t lunaricorn/leader . 2>&1 | tee -i tmp/build.log

# Check if build was successful
if [ $? -eq 0 ]; then
    echo "Build completed successfully!"
    echo "Image: lunaricorn/leader"
else
    echo "Build failed! Check tmp/build.log for details."
    exit 1
fi