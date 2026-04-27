#!/bin/bash

set -euo pipefail
ENV_FILE="../.env"

if [ ! -f "$ENV_FILE" ]; then
    echo "Ошибка: Файл $ENV_FILE не найден."
    echo "Убедитесь, что скрипт запускается из директории, где лежит папка leader-cpp,"
    echo "и что на уровень выше находится файл .env с необходимыми переменными."
    exit 1
fi

echo "==> Загружаем переменные окружения из $ENV_FILE"
source "$ENV_FILE"

# Конфигурация
IMAGE_NAME="lunaricorn_leader_cpp:latest"
CONTAINER_NAME="lunaricorn-leader-dev"
NETWORK_NAME="lunaricorn-network"
CONTEXT_DIR=$(pwd)
# Сборка образа (если ещё не собран)
echo "==> Сборка образа $IMAGE_NAME из $CONTEXT_DIR"
docker build -t "$IMAGE_NAME" .

# Проверяем, существует ли сеть; если нет — создаём
if ! docker network inspect "$NETWORK_NAME" >/dev/null 2>&1; then
    echo "==> Сеть $NETWORK_NAME не найдена, создаём"
    docker network create "$NETWORK_NAME"
fi

# Запуск контейнера в интерактивном режиме (Ctrl+C остановит и удалит)
echo "==> Запуск контейнера в интерактивном режиме"
docker run -it --rm \
    --name "$CONTAINER_NAME" \
    --network "$NETWORK_NAME" \
    -e MAINTENANCE_HOST=192.168.0.18 \
    -e MAINTENANCE_PORT=${MAINTENANCE_API_PORT} \
    -e PYTHONUNBUFFERED=1 \
    -e db_type=postgresql \
    -e db_host=lunaricorn-pg \
    -e db_port=5432 \
    -e db_user=lunaricorn \
    -e db_password=${LUNARICORN_PASSWORD} \
    -e db_name=lunaricorn \
    -e db_schema=lunaricorn \
    -e WORKERS=4 \
    -p "8001:8000" \
    -v "$(pwd)/example_data:/opt/lunaricorn/leader_data:rw" \
    "$IMAGE_NAME"

echo "==> Контейнер остановлен и удалён"