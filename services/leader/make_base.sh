#!/bin/bash

mkdir -p tmp

set -e
docker build --no-cache --progress=plain -t lunaricorn_leader_base -f Dockerfile.base . 2>&1 | tee -i tmp/build_base.log

# Check if build was successful
if [ $? -eq 0 ]; then
    echo "Base build completed successfully!"
    echo "Image: lunaricorn_leader_base"
else
    echo "Build failed! Check tmp/build_base.log for details."
    exit 1
fi
