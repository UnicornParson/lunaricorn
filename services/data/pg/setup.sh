#!/bin/bash

echo "max_connections = 200" >> "$PGDATA/postgresql.conf"
echo "shared_buffers = 256MB" >> "$PGDATA/postgresql.conf"
echo "effective_cache_size = 1GB" >> "$PGDATA/postgresql.conf"

echo "pg_stat_statements.max = 10000" >> "$PGDATA/postgresql.conf"
echo "pg_stat_statements.track = all" >> "$PGDATA/postgresql.conf"
echo "pgaudit.log = 'write, ddl'" >> "$PGDATA/postgresql.conf"
echo "pgaudit.log_catalog = on" >> "$PGDATA/postgresql.conf"
echo "pgaudit.log_parameter = on" >> "$PGDATA/postgresql.conf"

RUN mkdir -p /var/log/pgaudit && chown postgres:postgres /var/log/pgaudit
echo "log_directory = '/var/log/pgaudit'" >> "$PGDATA/postgresql.conf"
echo "log_filename = 'postgresql.log'" >> "$PGDATA/postgresql.conf"
echo "logging_collector = on" >> "$PGDATA/postgresql.conf"
echo "log_line_prefix = '%t [%p] %q%u@%d '" >> "$PGDATA/postgresql.conf"
echo "log_rotation_age = 1d" >> "$PGDATA/postgresql.conf"
echo "log_rotation_size = 100MB" >> "$PGDATA/postgresql.conf"
