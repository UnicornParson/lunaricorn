#!/bin/bash

mkdir -p tmp

set -e
rm -rvf tmp/lunaricorn.tgz
tar -cvzf tmp/lunaricorn.tgz ../../lunaricorn
docker build --no-cache --progress=plain -f Dockerfile.pyflakes -t lunaricorn-orb-check .
echo "build - ok. run"
docker run --rm lunaricorn-orb-check