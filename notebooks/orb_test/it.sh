#/bin/bash

docker run --rm -it \
  -v "$(cd ../../lunaricorn && pwd):/opt/lunaricorn/app/lunaricorn:ro" \
  --entrypoint /bin/bash \
  lunaricorn-orb-tests