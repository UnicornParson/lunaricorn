#!/bin/bash
set -e



psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" <<-EOSQL
    CREATE USER "lunaricorn" WITH PASSWORD '$LUNARICORN_PASSWORD';
    CREATE DATABASE "lunaricorn" OWNER "lunaricorn";
    GRANT ALL PRIVILEGES ON DATABASE "lunaricorn" TO "lunaricorn";
EOSQL

psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "lunaricorn" <<-EOSQL
    CREATE EXTENSION IF NOT EXISTS pg_stat_statements;
EOSQL

psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "lunaricorn" <<-EOSQL
    CREATE EXTENSION IF NOT EXISTS pgaudit;
EOSQL

psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" <<-EOSQL
    SELECT pg_reload_conf();
EOSQL