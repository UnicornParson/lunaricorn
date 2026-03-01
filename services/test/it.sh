#!/bin/bash

set -e
mkdir -p tmp

docker rm -f lunaricorn_test

docker run -it --rm --name lunaricorn_test lunaricorn_test