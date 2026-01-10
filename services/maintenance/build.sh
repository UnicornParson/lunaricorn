#!/bin/bash

set -e


echo "Starting Docker build for lunaricorn_maintenance..."

# Build the Docker image with detailed output
docker build --no-cache --progress=plain -t lunaricorn_maintenance . 

