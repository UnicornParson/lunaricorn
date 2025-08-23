#!/bin/bash

set -e
set -a
source .native_env
set +a
env
pushd .local_run/
source venv/bin/activate

python3 ../main.py
popd