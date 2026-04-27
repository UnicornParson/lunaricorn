#!/bin/bash

mkdir -p tmp

set -e
docker build --no-cache --progress=plain -t lunaricorn_leader_cpp_base -f Dockerfile.base . 2>&1 | tee -i tmp/build_base.log

# Check if build was successful
if [ $? -eq 0 ]; then
    echo "Base build completed successfully!"
    echo "Image: lunaricorn_leader_cpp_base"
else
    echo "Build failed! Check tmp/build_base.log for details."
    exit 1
fi


docker build --no-cache --progress=plain -t lunaricorn_leader_cpp_builder -f Dockerfile.builder  . 2>&1 | tee -i tmp/build_builder.log

# Check if build was successful
if [ $? -eq 0 ]; then
    echo "Builder image build completed successfully!"
    echo "Image: lunaricorn_leader_cpp_builder"
else
    echo "Build failed! Check tmp/build_builder.log for details."
    exit 1
fi
