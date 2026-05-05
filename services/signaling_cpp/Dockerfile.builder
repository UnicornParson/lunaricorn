
FROM ubuntu:25.10
USER root
ENV PYTHONUNBUFFERED=1
ENV PYTHONDONTWRITEBYTECODE=1
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential python3 libbz2-dev libz-dev libicu-dev cmake wget git-all \
    g++ libssl-dev libmysqlclient-dev libpq-dev mc

# BOOST
RUN mkdir -p /opt/3rd/boost/src
RUN mkdir -p /opt/3rd/boost/bin
WORKDIR /opt/3rd/boost/
RUN wget -O boost.tgz https://github.com/boostorg/boost/releases/download/boost-1.90.0/boost-1.90.0-cmake.tar.gz
RUN tar xf boost.tgz -C /opt/3rd/boost/src
WORKDIR /opt/3rd/boost/src/boost-1.90.0
RUN mkdir -p __build && rm -rvf __build/* && cd __build && cmake .. -DCMAKE_INSTALL_PREFIX=/opt/3rd/boost/bin && cmake --build . -j$('nproc') && cmake --build . --target install


# POCO
RUN mkdir -p /opt/3rd/poco/bin
WORKDIR /opt/3rd/poco/
RUN git clone -b main https://github.com/pocoproject/poco.git src && cd src && git checkout -f poco-1.15.0-release
WORKDIR /opt/3rd/poco/src
RUN mkdir -p cmake-build && rm -rvf cmake-build/* && cd cmake-build && cmake .. -DCMAKE_INSTALL_PREFIX=/opt/3rd/poco/bin && cmake --build . -j$('nproc') --config Release && cmake --build . --target install


ENTRYPOINT ["/bin/bash"]