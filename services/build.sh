#!/bin/bash
set -e

start_time=$(date +%s)

if ! docker image inspect lunaricorn-pg &>/dev/null; then
    echo "Image lunaricorn-pg not found. Building data/pg..."
    pushd data/pg > /dev/null
    ./rebuild_image.sh
    popd > /dev/null
fi

echo "Building maintenance service..."
pushd maintenance > /dev/null
./make_app.sh
popd > /dev/null

echo "Building leader service..."
pushd leader > /dev/null
./make_app.sh
popd > /dev/null

echo "Building portal service..."
pushd portal > /dev/null
./build.sh
popd > /dev/null

echo "Building signaling service..."
pushd signaling > /dev/null
./build.sh
popd > /dev/null

echo "Building orb service..."
pushd orb > /dev/null
./make_app.sh
popd > /dev/null


end_time=$(date +%s)
build_time=$((end_time - start_time))

# Output the total build time in seconds
echo "Total build time: ${build_time} seconds"

