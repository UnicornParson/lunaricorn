#!/bin/bash

set -e

mkdir -p __build
rm -rvf __build/*
cd __build

cmake ..

cmake --build . -j$('nproc') --config Release
echo "@@"
ls -lh