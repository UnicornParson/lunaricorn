#!/bin/bash
set -e
flag_f=false
if [ ! -f ".env" ]; then
    echo "Error: .env file not found"
    exit 1
fi

while [[ $# -gt 0 ]]; do
    case $1 in
        -f)
            flag_f=true
            shift
            ;;
        *)
            echo "unknown argument: $1"
            exit 1
            ;;
    esac
done
if $flag_f; then
    echo "Building signaling service..."
    pushd signaling > /dev/null
    ./build.sh
    popd > /dev/null
fi
docker compose stop signaling && docker compose rm -f signaling
docker compose up -d --build --force-recreate signaling
#docker compose up --force-recreate signaling
echo "Signaling service restarted"