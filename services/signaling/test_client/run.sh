#!/bin/bash

set -e
set -a
source ../.native_env
set +a

source ../venv/bin/activate
python3 client.py
