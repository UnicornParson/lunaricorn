# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- `GET /v1/list` no longer blocks on `ready()` state. Services can now register and report aliveness
  even when the leader is not yet fully ready. This prevents deadlock where new services need
  `get_list()` to discover the cluster but cannot register because the leader waits for all
  `required_nodes` before serving the list endpoint.