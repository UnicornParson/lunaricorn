
FROM python:3.13-slim

RUN pip install grpcio-tools
WORKDIR /app
ENTRYPOINT ["python", "-m", "grpc_tools.protoc"]