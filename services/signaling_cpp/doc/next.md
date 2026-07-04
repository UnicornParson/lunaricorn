# Глобальная телеметрия

Нужно создать глобальный класс для сбора статистики. нужно собирать следующую статистику
* общее количество успешных push через raw endpoint
* текущее количество активных клиентов в raw endpoint
* количество успешных push через raw endpoints за последнюю минуту
* количество ошибок в работе протокола raw endpoint за последнюю минуту

статистика должна печататься через MLOG_D раз в минуту в виде информативной строки
класс телеметрии должен иметь возможность отдавать boost json со своими данными

**Status: ✅ Done** — `Telemetry` singleton implemented in `telemetry.h/cpp` with all required metrics.

---

# HTTP endpoint

должен поддерживать следующие api функции

push message - аналогичен RawEndpoint processPushRequest
pull messages - аналогичен RawEndpoint processQueryRequest
stat - возвращает json с текущй телеметрией

**Status: ✅ Done** — `HttpServer` implemented in `http_endpoint.h/cpp` using Boost.Beast.

## Implementation Summary

### Files Created/Modified
| File | Status | Description |
|------|--------|-------------|
| `app/http_endpoint.h` | Created | HttpServer, Session classes, HttpServerConfig |
| `app/http_endpoint.cpp` | Created | Full implementation of HTTP server and route handlers |
| `app/main.cpp` | Modified | Added HttpServer initialization and lifecycle |
| `app/CMakeLists.txt` | Modified | Added Boost::beast component |
| `doc/changelog.md` | Modified | Added [Unreleased] section with HTTP feature |
| `doc/hld.md` | Modified | Added HttpServer component and REST API docs |

### REST API Endpoints
| Method | Path | Description |
|--------|------|-------------|
| GET | `/` | Service status |
| GET | `/health` | Health check |
| POST | `/push` | Publish event (analog of RawEndpoint::processPushRequest) |
| GET | `/pull?offset=N` | Query events (analog of RawEndpoint::processQueryRequest) |
| GET | `/stat` | JSON telemetry snapshot |

### Architecture
```
HttpServer (Endpoint impl)
├── boost::asio::io_context (ioc_)
├── tcp::acceptor (acceptor_)
├── std::vector<std::thread> (threads_)
└── Session (per-connection)
    ├── boost::beast::flat_buffer (buffer_)
    ├── http::request (request_)
    ├── tcp::socket (socket_)
    └── routing:
        ├── handle_root() → GET /
        ├── handle_health() → GET /health
        ├── handle_push() → POST /push
        ├── handle_pull() → GET /pull
        └── handle_stat() → GET /stat
```

### Configuration
| Parameter | Default | Description |
|-----------|---------|-------------|
| `address` | `"0.0.0.0"` | Bind address |
| `port` | `8081` | HTTP port |
| `num_threads` | `1` | IO thread count |