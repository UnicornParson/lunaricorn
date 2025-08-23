#!/bin/bash
set -e
mkdir -p ./.local_run
rm -rvf ./.local_run/*
pushd ./.local_run/

ln -s ../../../lunaricorn lunaricorn
ln -s ../cfg cfg

python3 -m venv venv
source venv/bin/activate
pip3 install -r lunaricorn/requirements.txt
pip3 install -r ../requirements.txt
popd