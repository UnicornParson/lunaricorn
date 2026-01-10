#!/bin/bash

set -e

FILE="lunaricorn_maintenance.txt"

# создать или очистить файл
: > "$FILE"

# запуск контейнера
docker run -it --rm \
  -p 8006:5672 \
  -p 15672:15672 \
  -v "$(pwd)/$FILE:/app/data/messages.log" \
  lunaricorn_maintenance