FROM lunaricorn_leader_base

USER root

RUN pip install --no-cache-dir -r /opt/lunaricorn/leader/app/requirements.txt
VOLUME ["/opt/lunaricorn/leader/app"]
# Copy application code to the specified directory
COPY . /opt/lunaricorn/leader/app/
ADD tmp/lunaricorn.tgz /opt/lunaricorn/leader/app
RUN pip install --no-cache-dir -r /opt/lunaricorn/leader/app/lunaricorn/requirements.txt

# Set working directory to the application directory
WORKDIR /opt/lunaricorn/leader/app

# Expose port
EXPOSE 8000

# Set volume for persistent data
VOLUME ["/opt/lunaricorn/leader_data"]

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8000/health || exit 1

# Run the application
CMD ["python", "leader_app.py"]
