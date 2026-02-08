FROM lunaricorn_maintenance_base

WORKDIR /opt/app
COPY app.py storage.py .

# Каталог для данных
RUN mkdir -p /app/data
RUN mkdir -p /opt/app/static
COPY static/* /opt/app/static

# http api
EXPOSE 8000
 
CMD ["python3", "app.py"]