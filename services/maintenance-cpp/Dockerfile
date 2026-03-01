
FROM lunaricorn_maintenance_cpp_base AS builder
ENV PYTHONUNBUFFERED=1
ENV PYTHONDONTWRITEBYTECODE=1
ENV DEBIAN_FRONTEND=noninteractive
RUN apt -y update && apt -y install git g++ cmake libssl-dev libmysqlclient-dev libpq-dev build-essential


COPY app/ /opt/app/

RUN chmod +x /opt/app/build.sh && /opt/app/build.sh

FROM ubuntu:25.10

COPY --from=builder /opt/app/bin/app /opt/app/bin/app

ENTRYPOINT ["/opt/app/bin/app"]