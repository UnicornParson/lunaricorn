#!/bin/bash

docker compose stop $(docker compose config --services | grep -v 'pg')
