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

RUN mkdir -p /opt/lunaricorn/portal/logs
COPY cfg/config.yaml /opt/lunaricorn/portal/data/config.yaml
COPY requirements.txt /opt/lunaricorn/portal/app/   

RUN pip install --no-cache-dir -r /opt/lunaricorn/portal/app/requirements.txt

# Copy application code to the specified directory
COPY . /opt/lunaricorn/portal/app/
ADD tmp/lunaricorn.tgz /opt/lunaricorn/portal/app
RUN ls -la /opt/lunaricorn/portal/app/lunaricorn
# Set working directory to the application directory
WORKDIR /opt/lunaricorn/portal/app

# Expose ZeroMQ ports
EXPOSE 5555 5556 5557

# Set volume for persistent data
VOLUME ["/opt/lunaricorn/portal/data"]



# Run the application
CMD ["python", "main.py"]
