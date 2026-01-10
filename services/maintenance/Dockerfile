FROM rabbitmq:3.12-management

# Установка python
RUN apt-get update && \
    apt-get install -y python3 python3-pip && \
    apt-get clean

# Рабочая директория
WORKDIR /app

# Python зависимости
COPY requirements.txt .
RUN pip3 install --no-cache-dir -r requirements.txt

# Конфиги и код
COPY consumer.py .
COPY rabbitmq.conf /etc/rabbitmq/rabbitmq.conf

# Каталог для данных
RUN mkdir -p /app/data

# Открываем порт RabbitMQ
EXPOSE 5672
EXPOSE 15672

# Старт: RabbitMQ + consumer
CMD rabbitmq-server -detached && python3 consumer.py
