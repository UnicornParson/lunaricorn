FROM lunaricorn_orb_base

USER root

RUN pip install --no-cache-dir -r /opt/lunaricorn/orb/app/requirements.txt
VOLUME ["/opt/lunaricorn/orb/app"]
# Copy application code to the specified directory
COPY . /opt/lunaricorn/orb/app/
ADD tmp/lunaricorn.tgz /opt/lunaricorn/orb/app
RUN pip install --no-cache-dir -r /opt/lunaricorn/orb/app/lunaricorn/requirements.txt

# Set working directory to the application directory
WORKDIR /opt/lunaricorn/orb/app/internal

# auto update
#RUN python -m grpc_tools.protoc -I. --python_out=.. --grpc_python_out=.. datastorage.proto

WORKDIR /opt/lunaricorn/orb/app

# Expose api ports
EXPOSE 8080
EXPOSE 50051

# Set volume for persistent data
VOLUME ["/opt/lunaricorn/orb/data"]
VOLUME ["/opt/lunaricorn/orb/file_storage"]
VOLUME ["/opt/lunaricorn/orb/share"]

# Run the application
CMD ["python", "main.py"]
