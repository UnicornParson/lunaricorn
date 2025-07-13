FROM python:3.13.5

# Set environment variables
ENV PYTHONUNBUFFERED=1
ENV PYTHONDONTWRITEBYTECODE=1
USER root
# Install system dependencies
RUN apt-get update && apt-get install -y \
    gcc \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Create necessary directories
RUN mkdir -p /opt/lunaricorn/leader_data/logs \
    && mkdir -p /opt/lunaricorn/leader/app/initial_data

# Copy requirements file
COPY requirements.txt /opt/lunaricorn/leader/app/

# Install Python dependencies
RUN pip install --no-cache-dir -r /opt/lunaricorn/leader/app/requirements.txt

# Copy application code to the specified directory
COPY . /opt/lunaricorn/leader/app/

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
