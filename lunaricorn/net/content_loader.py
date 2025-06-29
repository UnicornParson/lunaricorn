import feedparser
from datetime import datetime
import requests
from email.utils import parsedate_to_datetime
from argon2.low_level import hash_secret_raw, Type
import base64
import time
import sys
import os
import subprocess
from urllib.parse import urlparse
from bs4 import BeautifulSoup
from selenium import webdriver
from selenium.webdriver.chrome.service import Service
from selenium.webdriver.chrome.options import Options
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait
from webdriver_manager.chrome import ChromeDriverManager
from selenium.webdriver.support import expected_conditions as EC
from webdriver_manager.chrome import ChromeDriverManager
from playwright.sync_api import sync_playwright
from playwright.async_api import async_playwright
import cloudscraper
import tempfile
import asyncio
from enum import Enum
import re
import html
from tabulate import tabulate
from dataclasses import dataclass, fields, asdict
from typing import List, Optional, Dict, Any
import logging

try:
    # for webdriver_manager >= 3.8.0
    from webdriver_manager.core.os_manager import ChromeType
except ImportError:
    # old webdriver_manager
    from webdriver_manager.core.utils import ChromeType

class ContentLoaderEnv:
    def __init__(self):
        self.logger = logging.getLogger("net.ContentLoaderEnv")

    def install_chromium_conda(self) -> bool:
        self.logger.info("try to install chromium")
        try:
            if 'ipykernel' in sys.modules: # run in Jupyter
                get_ipython().system('conda install -c conda-forge chromium-browser -y')
                get_ipython().system('conda install -c conda-forge chromedriver -y')
            else:
                subprocess.run(['conda', 'install', '-c', 'conda-forge', 'chromium-browser', '-y'], check=True)
                subprocess.run(['conda', 'install', '-c', 'conda-forge', 'chromedriver', '-y'], check=True)
            self.logger.info("chromium has been installed")
            return True
        except Exception as e:
            self.logger.warning(f"installation failed: {e}")
            return False
        
    def get_chromium_path(self):
        conda_prefix = os.environ.get('CONDA_PREFIX', '')
        paths = [
            os.path.join(conda_prefix, 'bin', 'chromium'),
            os.path.join(conda_prefix, 'bin', 'chromium-browser'),
            os.path.join(conda_prefix, 'Library', 'bin', 'chromium.exe'),
        ]
        
        for path in paths:
            if os.path.exists(path):
                return path
        return None
    
    def install_playwright_deps_local():
        print("Установка локальных зависимостей для Playwright...")
        try:
            # Создаем директорию для зависимостей
            home_dir = os.path.expanduser("~")
            deps_dir = os.path.join(home_dir, "playwright-deps")
            os.makedirs(deps_dir, exist_ok=True)
            
            # Устанавливаем необходимые пакеты в локальную директорию
            deps = [
                "libatk1.0-0", "libatk-bridge2.0-0", "libxcomposite1",
                "libxdamage1", "libatspi2.0-0", "libnss3", "libnspr4",
                "libdrm2", "libxkbcommon0", "libgbm1", "libasound2"
            ]
            
            for dep in deps:
                # Скачиваем пакет
                download_cmd = f"apt-get download {dep}"
                subprocess.run(download_cmd, shell=True, check=True, cwd=deps_dir)
                
                # Находим скачанный файл
                deb_file = next((f for f in os.listdir(deps_dir) if f.endswith(".deb") and dep in f), None)
                if not deb_file:
                    print(f"Не удалось найти .deb файл для {dep}")
                    continue
                    
                # Распаковываем пакет
                deb_path = os.path.join(deps_dir, deb_file)
                extract_cmd = f"dpkg-deb -x {deb_path} {deps_dir}"
                subprocess.run(extract_cmd, shell=True, check=True)
                
                # Удаляем .deb файл после распаковки
                os.remove(deb_path)
            
            # Настраиваем переменные окружения
            lib_path = os.path.join(deps_dir, "usr", "lib", "x86_64-linux-gnu")
            os.environ["LD_LIBRARY_PATH"] = f"{lib_path}:{os.environ.get('LD_LIBRARY_PATH', '')}"
            print(f"LD_LIBRARY_PATH установлен: {os.environ['LD_LIBRARY_PATH']}")
            
            return True
        except Exception as e:
            print(f"Ошибка установки зависимостей: {e}")
            return False
class ContentLoader:
    def __init__(self):
        self.logger = logging.getLogger("net.ContentLoader")

    def try_scraper(self, url) -> str:
        scraper = cloudscraper.create_scraper()
        return scraper.get(url).text
    

        

    
