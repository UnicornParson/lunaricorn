#!/bin/bash

set -e
mkdir -p tmp
CONTAINER_NAME=lunaricorn_maintenance_cpp
docker rm -f $CONTAINER_NAME

docker run -it --rm --name $CONTAINER_NAME \
  -p 8006:8000 \
  -v "$(pwd)/tmp:/var/app/data/" \
  $CONTAINER_NAME