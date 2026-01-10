FROM lunaricorn_orb_tester_base

RUN mkdir -p /opt/lunaricorn/app/lunaricorn
WORKDIR /opt/lunaricorn/app

ADD orb_test.py /opt/lunaricorn/app
CMD ["pytest", "-v", "/opt/lunaricorn/app/orb_test.py"]