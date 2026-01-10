#!/bin/bash
set -e
cd "$(dirname "$0")"

# Check if proto file exists, exit with error
if [ ! -f "datastorage.proto" ]; then
    echo "Error: datastorage.proto not found in current directory"
    exit 1
fi

echo "Building Docker image for protobuf generation..."
docker build --no-cache --progress=plain -t protobuf-gen-python .

echo "Generating Python protobuf files..."
docker run --rm \
    -v "$(pwd):/app" \
    protobuf-gen-python \
    -I. \
    --python_out=. \
    --grpc_python_out=. \
    datastorage.proto

echo "Fixing imports in datastorage_pb2_grpc.py..."
sed -i 's/^import datastorage_pb2 as datastorage__pb2$/from . import datastorage_pb2 as datastorage__pb2/' datastorage_pb2_grpc.py

echo "Done! Generated files:"
ls -la datastorage_pb2*.py