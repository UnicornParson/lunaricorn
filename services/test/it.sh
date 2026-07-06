#!/bin/bash

set -e
mkdir -p tmp

docker rm -f lunaricorn_test 2>/dev/null || true


echo "Running in TEST_MODE (leader connection disabled)"
docker run -it --rm \
    --name lunaricorn_test \
    -e TEST_MODE=1 \
    lunaricorn_test
