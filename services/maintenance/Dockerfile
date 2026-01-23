FROM rabbitmq:4.2-management

# Установка python
RUN apt-get update && \
    apt-get install -y python3 python3-pip && \
    apt-get install -y net-tools iputils-ping curl && \
    apt-get clean

# Рабочая директория
WORKDIR /app

# Python зависимости
COPY requirements.txt .
RUN pip3 install --no-cache-dir --break-system-packages -r requirements.txt

COPY consumer.py orchestrator.py producer.py .

COPY rabbitmq.conf /etc/rabbitmq/rabbitmq.conf

# Каталог для данных
RUN mkdir -p /app/data

# Открываем порт RabbitMQ
# rmq port
EXPOSE 5672

# rmq ui
EXPOSE 15672 

# http api
EXPOSE 8000

CMD ["python3", "orchestrator.py"]