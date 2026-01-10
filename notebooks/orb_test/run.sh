#!/usr/bin/env bash
set -euo pipefail

IMAGE_NAME="lunaricorn-orb-tests"
DOCKERFILE="Dockerfile.app"

# Абсолютный путь к volume на хосте
HOST_VOLUME_PATH="$(cd ../../lunaricorn && pwd)"
CONTAINER_VOLUME_PATH="/opt/lunaricorn/app/lunaricorn"

echo "🔧 Building image..."
docker build --no-cache --progress=plain -f "${DOCKERFILE}" -t "${IMAGE_NAME}" .

echo "▶️ Running tests in container..."
docker run --rm \
  -v "${HOST_VOLUME_PATH}:${CONTAINER_VOLUME_PATH}:ro" \
  "${IMAGE_NAME}"
