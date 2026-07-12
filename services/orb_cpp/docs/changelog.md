# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Fixed
- Исправлена ошибка компиляции `random_device`/`mt19937` — добавлен `#include <random>` в `main.cpp`.
- Исправлена ошибка компиляции `LeaderConnector`/`ConnectorUtils` — добавлен `#include <leader_api.h>` в `main.cpp`.
- Исправлена опечатка `#pragma one` → `#pragma once` в `oid.h`.
- Добавлена временная заглушка `make_engine()` для обеспечения компиляции до реализации движка БД.