
FROM postgres:18.1
ENV DEBIAN_FRONTEND=noninteractive


# Install postgresql-contrib for pg_stat_statements
RUN apt-get update && apt-get install -y postgresql-contrib postgresql-18-pgaudit nano mc

RUN mkdir -p /var/log/pgaudit && chown postgres:postgres /var/log/pgaudit && chmod a+rw /var/log/pgaudit
COPY init-db.sh /docker-entrypoint-initdb.d/
RUN chmod a+x /docker-entrypoint-initdb.d/init-db.sh
# RUN /docker-entrypoint-initdb.d/init-db.sh
#RUN ls -lh /var/lib/postgresql/
#RUN mv /var/lib/postgresql/18/docker/postgresql.conf /var/lib/postgresql/18/docker/postgresql.conf.orig
COPY postgresql.conf /var/postgresql.conf

EXPOSE 5432
VOLUME /var/lib/postgresql/18/docker/postgresql.conf
VOLUME /var/log/pgaudit
