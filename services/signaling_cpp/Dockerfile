
FROM lunaricorn_signaling_cpp_builder AS builder
ENV CMAKE_PREFIX_PATH=/opt/3rd/soci/bin:${CMAKE_PREFIX_PATH:-}
COPY app/ /opt/app/
WORKDIR /opt/app/
RUN chmod +x /opt/app/build.sh 
#ENV BUILD_TYPE=Debug
ENV BUILD_TYPE=Release
RUN mkdir -p /opt/tmp && mkdir -p /opt/app/apilib
ADD tmp/lunaricorn.tgz /opt/tmp
RUN cp -rvf /opt/tmp/lunaricorn/cpp/*  /opt/app/apilib
RUN /opt/app/build.sh


FROM lunaricorn_signaling_cpp_builder AS tester
ENV CMAKE_PREFIX_PATH=/opt/3rd/soci/bin:$CMAKE_PREFIX_PATH
COPY test_client/ /opt/test_client/
WORKDIR /opt/test_client/
RUN chmod +x /opt/test_client/build.sh 
#ENV BUILD_TYPE=Debug
ENV BUILD_TYPE=Release
RUN mkdir -p /opt/tmp && mkdir -p /opt/test_client/apilib
ADD tmp/lunaricorn.tgz /opt/tmp
RUN cp -rvf /opt/tmp/lunaricorn/cpp/*  /opt/test_client/apilib
RUN /opt/test_client/build.sh

FROM lunaricorn_signaling_cpp_base
ENV CMAKE_PREFIX_PATH=/opt/3rd/soci/bin:${CMAKE_PREFIX_PATH:-}
COPY --from=builder /opt/app/__build/signaling /opt/app/bin/signaling
COPY --from=tester /opt/test_client/__build/signaling_test /opt/app/bin/tester

COPY --from=builder /opt/3rd/boost/bin/lib /opt/3rd/boost/bin/lib
COPY --from=builder /opt/3rd/poco/bin/lib /opt/3rd/poco/bin/lib


RUN ln -s /bin/bash /usr/bin/bh

# Добавляем пути к библиотекам в конфигурацию динамического компоновщика
RUN echo "/opt/3rd/boost/bin/lib" >> /etc/ld.so.conf.d/boost.conf && \
    echo "/opt/3rd/poco/bin/lib" >> /etc/ld.so.conf.d/poco.conf && \
    ldconfig

#ENTRYPOINT ["gdb", "--args", "/opt/app/bin/signaling"]
ENTRYPOINT ["/opt/app/bin/signaling"]
