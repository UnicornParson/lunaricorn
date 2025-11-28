
FROM postgres:18.1
ENV DEBIAN_FRONTEND=noninteractive
RUN echo "listen_addresses = '*'" >> /usr/share/postgresql/postgresql.conf.sample

# Install postgresql-contrib for pg_stat_statements
RUN apt-get update && apt-get install -y postgresql-contrib

# Configure shared_preload_libraries with only pg_stat_statements (pg_wait_sampling is optional)
RUN echo "shared_preload_libraries = 'pg_stat_statements'" >> /usr/share/postgresql/postgresql.conf.sample
RUN echo "pg_stat_statements.max = 10000" >> /usr/share/postgresql/postgresql.conf.sample
RUN echo "pg_stat_statements.track = all" >> /usr/share/postgresql/postgresql.conf.sample

COPY init-db.sh /docker-entrypoint-initdb.d/
RUN chmod +x /docker-entrypoint-initdb.d/init-db.sh
EXPOSE 5432
VOLUME /var/lib/postgresql

