#!/bin/bash
set -e

echo "host all all 0.0.0.0/0 md5" >> "$PGDATA/pg_hba.conf"

psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" <<-EOSQL
    CREATE USER "lunaricorn" WITH PASSWORD '$LUNARICORN_PASSWORD';
    CREATE DATABASE "lunaricorn" OWNER "lunaricorn";
    GRANT ALL PRIVILEGES ON DATABASE "lunaricorn" TO "lunaricorn";
EOSQL

psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "lunaricorn" <<-EOSQL
    CREATE EXTENSION IF NOT EXISTS pg_stat_statements;
    CREATE EXTENSION IF NOT EXISTS pg_wait_sampling;
EOSQL

psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" <<-EOSQL
    SELECT pg_reload_conf();
EOSQL