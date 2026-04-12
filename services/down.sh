#!/bin/bash

docker compose stop $(docker compose ps --services | grep -v 'pg')
