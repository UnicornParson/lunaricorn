
FROM lunaricorn_leader_cpp_builder AS builder

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

FROM lunaricorn_leader_cpp_base

COPY --from=builder /opt/app/__build/leader /opt/app/bin/leader
COPY --from=builder /opt/3rd/boost/bin/lib /opt/3rd/boost/bin/lib
COPY --from=builder /opt/3rd/poco/bin/lib /opt/3rd/poco/bin/lib

RUN ln -s /bin/bash /usr/bin/bh

# Добавляем пути к библиотекам в конфигурацию динамического компоновщика
RUN echo "/opt/3rd/boost/bin/lib" >> /etc/ld.so.conf.d/boost.conf && \
    echo "/opt/3rd/poco/bin/lib" >> /etc/ld.so.conf.d/poco.conf && \
    ldconfig

#ENTRYPOINT ["gdb", "--args", "/opt/app/bin/leader"]
ENTRYPOINT ["/opt/app/bin/leader"]