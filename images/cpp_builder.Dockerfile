
FROM ubuntu:25.10
USER root
ENV PYTHONUNBUFFERED=1
ENV PYTHONDONTWRITEBYTECODE=1
ENV DEBIAN_FRONTEND=noninteractive
RUN apt update && apt install -y \
    build-essential python3 libbz2-dev libz-dev libicu-dev cmake wget git-all bc bzip2 ca-certificates ccache findutils\
    g++ libssl-dev libmysqlclient-dev libpq-dev mc libfdt-dev libffi-dev libglib2.0-dev locales make meson ninja-build pkgconf sed 7za \
    libncurses-dev gawk flex bison openssl libssl-dev dkms libelf-dev libudev-dev libpci-dev libiberty-dev autoconf llvm
RUN apt update && apt install python3 python3-pip python3-venv




ENTRYPOINT ["/bin/bash"]