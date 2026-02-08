#!/bin/bash

set -e
mkdir -p tmp

docker rm -f lunaricorn-maintenance

docker run -it --rm --name lunaricorn-maintenance \
  -p 8006:8000 \
  -v "$(pwd)/tmp:/var/app/data/" \
  lunaricorn_maintenance