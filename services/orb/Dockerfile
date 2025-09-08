FROM python:3.13.5

# Set environment variables
ENV PYTHONUNBUFFERED=1
ENV PYTHONDONTWRITEBYTECODE=1
ENV DEBIAN_FRONTEND=noninteractive
USER root
RUN apt update && apt install -y \
    gcc \
    g++ \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /opt/lunaricorn/orb/logs
RUN mkdir -p /opt/lunaricorn/orb/file_storage
COPY cfg/config.yaml /opt/lunaricorn/orb/data/config.yaml
COPY requirements.txt /opt/lunaricorn/orb/app/   

RUN pip install --no-cache-dir -r /opt/lunaricorn/orb/app/requirements.txt

# Copy application code to the specified directory
COPY . /opt/lunaricorn/orb/app/
ADD tmp/lunaricorn.tgz /opt/lunaricorn/orb/app
RUN ls -la /opt/lunaricorn/orb/app/lunaricorn
RUN pip install --no-cache-dir -r /opt/lunaricorn/orb/app/lunaricorn/requirements.txt

# Set working directory to the application directory
WORKDIR /opt/lunaricorn/orb/app

# Expose ZeroMQ ports
EXPOSE 8080

# Set volume for persistent data
VOLUME ["/opt/lunaricorn/orb/data"]
VOLUME ["/opt/lunaricorn/orb/file_storage"]


# Run the application
CMD ["python", "main.py"]
