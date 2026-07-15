#!/bin/bash

set -euo pipefail
ENV_FILE="../.env"

if [ ! -f "$ENV_FILE" ]; then
    echo "Ошибка: Файл $ENV_FILE не найден."
    echo "Убедитесь, что скрипт запускается из директории, где лежит папка orb_cpp,"
    echo "и что на уровень выше находится файл .env с необходимыми переменными."
    exit 1
fi

echo "==> Загружаем переменные окружения из $ENV_FILE"
source "$ENV_FILE"

# Конфигурация
CLUSTER_HOST="192.168.0.18"
IMAGE_NAME="lunaricorn_orb_cpp:latest"
CONTAINER_NAME="lunaricorn-orb-dev"
CONTEXT_DIR=$(pwd)
# Сборка образа (если ещё не собран)
echo "==> Сборка образа $IMAGE_NAME из $CONTEXT_DIR"
docker build -t "$IMAGE_NAME" .
mkdir -p example_data

# Запуск контейнера в интерактивном режиме (Ctrl+C остановит и удалит)
echo "==> Запуск контейнера в интерактивном режиме"
docker run -it --rm \
    --name "$CONTAINER_NAME" \
    -e MAINTENANCE_HOST=${CLUSTER_HOST} \
    -e MAINTENANCE_PORT=8007 \
    -e CLUSTER_LEADER_URL=http://${CLUSTER_HOST}:8001/ \
    -e PYTHONUNBUFFERED=1 \
    -e db_type=postgresql \
    -e db_host=${CLUSTER_HOST} \
    -e db_port=8003 \
    -e db_user=lunaricorn \
    -e db_password=${LUNARICORN_PASSWORD} \
    -e db_name=lunaricorn \
    -e db_schema=lunaricorn \
    -e WORKERS=4 \
    -p "5560:8080" \
    -p "5561:8081" \
    -v "$(pwd)/example_data:/opt/lunaricorn/orb_data:rw" \
    "$IMAGE_NAME"

echo "==> Контейнер остановлен и удалён"