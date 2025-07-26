#!/bin/bash
set -e

echo "host all all 0.0.0.0/0 md5" >> "$PGDATA/pg_hba.conf"

# Configure PostgreSQL for better connection handling
echo "max_connections = 200" >> "$PGDATA/postgresql.conf"
echo "shared_buffers = 256MB" >> "$PGDATA/postgresql.conf"
echo "effective_cache_size = 1GB" >> "$PGDATA/postgresql.conf"

psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" <<-EOSQL
    CREATE USER "lunaricorn" WITH PASSWORD '$LUNARICORN_PASSWORD';
    CREATE DATABASE "lunaricorn" OWNER "lunaricorn";
    GRANT ALL PRIVILEGES ON DATABASE "lunaricorn" TO "lunaricorn";
EOSQL

psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "lunaricorn" <<-EOSQL
    CREATE EXTENSION IF NOT EXISTS pg_stat_statements;
EOSQL

psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" <<-EOSQL
    SELECT pg_reload_conf();
EOSQL