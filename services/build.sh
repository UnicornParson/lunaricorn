#!/bin/bash
set -e

start_time=$(date +%s)

echo "Building data/pg service..."
pushd data/pg > /dev/null
./rebuild_image.sh
popd > /dev/null


echo "Building leader service..."
pushd leader > /dev/null
./build.sh
popd > /dev/null

echo "Building portal service..."
pushd portal > /dev/null
./build.sh
popd > /dev/null

echo "Building signaling service..."
pushd signaling > /dev/null
./build.sh
popd > /dev/null

end_time=$(date +%s)
build_time=$((end_time - start_time))

# Output the total build time in seconds
echo "Total build time: ${build_time} seconds"

