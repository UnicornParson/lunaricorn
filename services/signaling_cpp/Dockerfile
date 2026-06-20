
FROM lunaricorn_signaling_cpp_builder AS builder
#RUN apt install -y sqlite3 libsqlite3-dev libsoci-dev libsoci-postgresql4.0

RUN git clone --recurse-submodules https://github.com/SOCI/soci.git /tmp/soci 
RUN cd /tmp/soci && git checkout -f v4.1.4 && git submodule init && git submodule update
RUN cmake -S /tmp/soci -B /tmp/soci/build \
    -DCMAKE_BUILD_TYPE=Release \
    -DSOCI_TESTS=OFF \
    -DSOCI_POSTGRESQL=ON \
    -DSOCI_MYSQL=OFF \
    -DSOCI_SQLITE3=OFF \
    -DSOCI_ODBC=OFF \
    -DSOCI_ORACLE=OFF \
    -DSOCI_FIREBIRD=OFF
RUN cmake --build  /tmp/soci/build -j$(nproc)
RUN cmake --install /tmp/soci/build

COPY app/ /opt/app/
WORKDIR /opt/app/
RUN chmod +x /opt/app/build.sh 
#ENV BUILD_TYPE=Debug
ENV BUILD_TYPE=Release
RUN mkdir -p /opt/tmp && mkdir -p /opt/app/apilib
ADD tmp/lunaricorn.tgz /opt/tmp
RUN cp -rvf /opt/tmp/lunaricorn/cpp/*  /opt/app/apilib
RUN /opt/app/build.sh

# ------------------------------------------------------------------------

FROM lunaricorn_signaling_cpp_base

COPY --from=builder /opt/app/__build/signaling /opt/app/bin/signaling
COPY --from=builder /opt/3rd/boost/bin/lib /opt/3rd/boost/bin/lib
COPY --from=builder /opt/3rd/poco/bin/lib /opt/3rd/poco/bin/lib

RUN ln -s /bin/bash /usr/bin/bh

# Добавляем пути к библиотекам в конфигурацию динамического компоновщика
RUN echo "/opt/3rd/boost/bin/lib" >> /etc/ld.so.conf.d/boost.conf && \
    echo "/opt/3rd/poco/bin/lib" >> /etc/ld.so.conf.d/poco.conf && \
    ldconfig

#ENTRYPOINT ["gdb", "--args", "/opt/app/bin/signaling"]
ENTRYPOINT ["/opt/app/bin/signaling"]