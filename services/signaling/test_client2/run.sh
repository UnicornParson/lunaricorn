#!/bin/bash

set -e
set -a
source ../.native_env
set +a

source ../.local_run/venv/bin/activate
python3 app.py
