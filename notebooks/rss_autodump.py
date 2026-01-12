import sys
import os
from pathlib import Path
from datetime import datetime
sys.path.append(os.path.dirname(os.getcwd()))
sys.path.append(Path(os.getcwd()).parent.absolute())
import lunaricorn
import lunaricorn.api.signaling as lsig
import agents.rss_loader as rss_loader
# Redirect all logging output to Jupyter cell output
import logging
import asyncio

async def main():
    # Create a handler that writes log messages to stdout (Jupyter cell output)
    handler = logging.StreamHandler()
    handler.setLevel(logging.DEBUG)

    # Set a simple log format
    formatter = logging.Formatter('%(asctime)s [%(levelname)s] %(name)s: %(message)s')
    handler.setFormatter(formatter)

    # Get the root logger and configure it
    root_logger = logging.getLogger()
    root_logger.setLevel(logging.DEBUG)
    # Remove all existing handlers
    for h in root_logger.handlers[:]:
        root_logger.removeHandler(h)
    root_logger.addHandler(handler)

    # Also set logging for common noisy libraries to WARNING
    for noisy in ['asyncio', 'urllib3', 'selenium', 'playwright', 'websockets']:
        logging.getLogger(noisy).setLevel(logging.WARNING)


    dumper = lunaricorn.net.FileDataDumper("./rss_dumps.txt")

    loader = rss_loader.RSSLoaderClient(agent_id="notebook_agent", working_dir="./tmp/")
    convertor = lunaricorn.data.DataConvertor()
    sig_cfg = lsig.SignalingClientConfig("192.168.0.18", 5555, 5556, 5557)
    loader.connect_to_lunaricorn(sig_cfg)
    #entries = await loader.load("https://www.cncf.io/feed/")
    entries = await loader.load("https://habr.com/ru/rss/articles/?fl=ru")
    entries.extend(await loader.load("https://habr.com/ru/rss/articles/top/weekly/?fl=ru"))
    #entries.extend(await loader.load("https://tproger.ru/feed"))
    #entries.extend(await loader.load("https://proglib.io/feed"))
    loader.disconnect_lunaricorn()
    loader = None

asyncio.run(main())