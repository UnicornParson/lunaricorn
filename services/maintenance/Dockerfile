FROM lunaricorn_maintenance_base

WORKDIR /opt/app
COPY producer.py .

# Каталог для данных
RUN mkdir -p /app/data

# http api
EXPOSE 8000

CMD ["python3", "producer.py"]