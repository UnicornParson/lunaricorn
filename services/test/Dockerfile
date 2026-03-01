FROM lunaricorn_test_base
RUN mkdir -p /app/data
WORKDIR /opt/app
COPY app.py

ADD tmp/lunaricorn.tgz /opt/lunaricorn
RUN pip install --no-cache-dir -r /opt/lunaricorn/requirements.txt
CMD ["python3", "app.py"]