
FROM lunaricorn_maintenance_cpp_base AS builder
ENV PYTHONUNBUFFERED=1
ENV PYTHONDONTWRITEBYTECODE=1
ENV DEBIAN_FRONTEND=noninteractive
RUN apt -y update && apt -y install git g++ cmake libssl-dev libmysqlclient-dev libpq-dev build-essential


COPY app/ /opt/app/
WORKDIR /opt/app/
RUN chmod +x /opt/app/build.sh && /opt/app/build.sh

FROM ubuntu:25.10
RUN apt -y update && apt -y install libssl-dev libmysqlclient-dev libpq-dev build-essential mc
COPY --from=builder /opt/app/__build/maintenance /opt/app/bin/maintenance
COPY --from=builder /opt/3rd/boost/bin/lib /opt/3rd/boost/bin/lib
COPY --from=builder /opt/3rd/poco/bin/lib /opt/3rd/poco/bin/lib

# Добавляем пути к библиотекам в конфигурацию динамического компоновщика
RUN echo "/opt/3rd/boost/bin/lib" > /etc/ld.so.conf.d/boost.conf && \
    echo "/opt/3rd/poco/bin/lib" >> /etc/ld.so.conf.d/poco.conf && \
    ldconfig

ENTRYPOINT ["/opt/app/bin/maintenance"]