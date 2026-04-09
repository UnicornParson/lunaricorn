#!/bin/bash

set -e

BUILD_TYPE="${BUILD_TYPE:-Release}"

if [[ "$BUILD_TYPE" != "Debug" && "$BUILD_TYPE" != "Release" ]]; then
    echo "Ошибка: BUILD_TYPE должен быть 'Debug' или 'Release' (получено '$BUILD_TYPE')"
    exit 1
fi

echo "@@ Тип сборки: $BUILD_TYPE"

mkdir -p __build
rm -rvf __build/*
cd __build

cmake .. -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

cmake --build . -j$(nproc --all) --config "$BUILD_TYPE"
