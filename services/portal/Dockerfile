FROM lunaricorn_portal_base

USER root

RUN pip install --no-cache-dir -r /opt/lunaricorn/portal/app/requirements.txt
VOLUME ["/opt/lunaricorn/portal/app"]
# Copy application code to the specified directory
COPY . /opt/lunaricorn/portal/app/
ADD tmp/lunaricorn.tgz /opt/lunaricorn/portal/app
RUN pip install --no-cache-dir -r /opt/lunaricorn/portal/app/lunaricorn/requirements.txt

# Set working directory to the application directory
WORKDIR /opt/lunaricorn/portal/app

# Expose port
EXPOSE 8000

# Set volume for persistent data
VOLUME ["/opt/lunaricorn/portal/data"]

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8000/health || exit 1

# Run the application
CMD ["python", "main.py"]
