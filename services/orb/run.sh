#!/bin/bash
# Remove the existing 'orb' container if it is already running or exists
if [ "$(docker ps -aq -f name=^orb$)" ]; then
    echo "Removing existing 'orb' container..."
    docker rm -f orb
fi

CONTAINER_NAME=lunaricorn_orb
IMAGE_NAME=lunaricorn_orb
mkdir -p tmp/file_storage
mkdir -p tmp/data
docker stop $CONTAINER_NAME
docker rm $CONTAINER_NAME


source .native_env

docker run -it --name $CONTAINER_NAME \
  -p 8004:8080 \
  -v "$(pwd)/tmp/data:/opt/lunaricorn/orb_data" \
  -v "$(pwd)/tmp/file_storage:/opt/lunaricorn/orb/file_storage" \
  -e db_type=$db_type \
  -e db_host=$db_host \
  -e db_port=$db_port \
  -e db_name=lunaricorn \
  -e db_schema=lunaricorn \
  -e db_user=$db_user \
  -e db_password=$db_password \
  -e ORB_API_PORT=$ORB_API_PORT \
  -e SIGNALING_PUSH_PORT=$SIGNALING_PUSH_PORT \
  -e CLUSTER_LEADER_URL=$CLUSTER_LEADER_URL \
  $IMAGE_NAME
