FROM python:3.14.0-trixie

# Set environment variables
ENV PYTHONUNBUFFERED=1
ENV PYTHONDONTWRITEBYTECODE=1
ENV DEBIAN_FRONTEND=noninteractive
USER root
RUN apt update && apt install -y \
    gcc \
    g++ \
    iproute2

RUN mkdir -p /opt/lunaricorn/signaling_data/logs
COPY cfg/config.yaml /opt/lunaricorn/signaling_data/config.yaml
COPY requirements.txt /opt/lunaricorn/signaling/app/   
RUN pip install --upgrade pip
RUN pip install --no-cache-dir -r /opt/lunaricorn/signaling/app/requirements.txt

# Copy application code to the specified directory
COPY . /opt/lunaricorn/signaling/app/
ADD tmp/lunaricorn.tgz /opt/lunaricorn/signaling/app
RUN ls -la /opt/lunaricorn/signaling/app/lunaricorn
RUN pip install --no-cache-dir -r /opt/lunaricorn/signaling/app/lunaricorn/requirements.txt

# Set working directory to the application directory
WORKDIR /opt/lunaricorn/signaling/app

# Expose ZeroMQ ports
EXPOSE 5555 5556 

# Expose api port
EXPOSE 5557

# Set volume for persistent data
VOLUME ["/opt/lunaricorn/signaling_data"]



# Run the application
CMD ["python", "main.py"]
