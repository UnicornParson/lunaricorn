#!/bin/bash

mkdir -p tmp

set -e
docker build --no-cache --network=host  --progress=plain -t lunaricorn_orb_tester_base -f Dockerfile.base . 2>&1 | tee -i ../tmp/build_tester_base.log

