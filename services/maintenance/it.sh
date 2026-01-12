#!/bin/bash

set -e

FILE="lunaricorn_maintenance.txt"

# создать или очистить файл
: > "$FILE"


docker rm -f lunaricorn-maintenance
# запуск контейнера
docker run -it --rm --name lunaricorn-maintenance \
  -p 8006:5672 \
  -p 15672:15672 \
  -v "$(pwd)/$FILE:/app/data/messages.log" \
  -v "$(pwd)/log:/var/log/rabbitmq/" \
  lunaricorn_maintenance