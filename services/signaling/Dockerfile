FROM lunaricorn_signaling_base

USER root

RUN pip install --no-cache-dir -r /opt/lunaricorn/signaling/app/requirements.txt
VOLUME ["/opt/lunaricorn/signaling/app"]
# Copy application code to the specified directory
COPY . /opt/lunaricorn/signaling/app/
ADD tmp/lunaricorn.tgz /opt/lunaricorn/signaling/app
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
