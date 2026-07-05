#include "http_endpoint.h"
#include <sstream>
#include <chrono>
#include <ctime>
#include <boost/json/src.hpp>

namespace lunaricorn
{

// ---- Session implementation ----

Session::Session(tcp::socket socket, std::shared_ptr<SignalingEngine> engine, HttpServer& server)
    : socket_(std::move(socket)), engine_(engine), server_(server)
{
    ++obj_counter_;
}

Session::~Session()
{
    --obj_counter_;
}

void Session::start()
{
    server_.increment_active();
    do_read();
}

void Session::do_read()
{
    auto self = shared_from_this();
    http::async_read(socket_, buffer_, request_,
        [self](boost::beast::error_code ec, std::size_t bytes) {
            self->on_read(ec, bytes);
        });
}

void Session::on_read(boost::beast::error_code ec, std::size_t)
{
    if (ec == http::error::end_of_stream) {
        socket_.shutdown(tcp::socket::shutdown_send, ec);
        server_.decrement_active();
        return;
    }
    if (ec) {
        MLOG_E("HTTP read error: {}", ec.message());
        server_.decrement_active();
        return;
    }
    process_request();
}

void Session::process_request()
{
    auto target = request_.target();
    auto method = request_.method();
    const auto start_time = std::chrono::steady_clock::now();
    http::status response_status = http::status::ok;
    try {
        // Route: GET /
        if (method == http::verb::get && target == "/") {
            handle_root();
        }
        // Route: GET /health
        else if (method == http::verb::get && target == "/health") {
            handle_health();
        }
        // Route: POST /v1/push
        else if (method == http::verb::post && target == "/v1/push") {
            handle_push();
        }
        // Route: GET /v1/pull
        else if (method == http::verb::get && target.starts_with("/v1/pull")) {
            handle_pull();
        }
        // Route: GET /v1/stat
        else if (method == http::verb::get && target == "/v1/stat") {
            handle_stat();
        }
        else {
            response_status = http::status::not_found;
            send_json_response(response_status,
                json::value({{"error", "Not found"}}));
        }
    } catch (const std::exception& e) {
        MLOG_E("HTTP exception: {}", e.what());
        response_status = http::status::internal_server_error;
        send_json_response(response_status,
            json::value({{"error", "Internal server error"}}));
    }

    const auto end_time = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    MLOG_D("HTTP {} {} -> {} ({:.0f}ms)",
        request_.method_string(),
        target,
        static_cast<int>(response_status),
        elapsed);
}

void Session::send_response(http::status status, const std::string& content_type,
                            const std::string& body)
{
    auto res = std::make_shared<http::response<http::string_body>>(status, request_.version());
    res->set(http::field::server, "signaling/1.0");
    res->set(http::field::content_type, content_type);
    res->set(http::field::connection, "close");
    res->body() = body;
    res->prepare_payload();

    auto self = shared_from_this();
    http::async_write(socket_, *res,
        [self, res](boost::beast::error_code ec, std::size_t) {
            if (!ec) {
                self->socket_.shutdown(tcp::socket::shutdown_send, ec);
            }
            self->server_.decrement_active();
        });
}

void Session::send_json_response(http::status status, const json::value& j)
{
    send_response(status, "application/json", json::serialize(j));
}

// ---- Route handlers ----

void Session::handle_root()
{
    json::object obj;
    obj["status"] = json::value("online");
    obj["service"] = json::value("signaling");
    send_json_response(http::status::ok, obj);
}

void Session::handle_health()
{
    json::object obj;
    obj["status"] = json::value("online");
    send_json_response(http::status::ok, obj);
}

void Session::handle_push()
{
    // Parse JSON body
    boost::system::error_code ec;
    json::value j = json::parse(request_.body(), ec);
    if (ec) {
        send_json_response(http::status::bad_request,
            json::value({{"error", "Invalid JSON"}}));
        return;
    }

    if (!j.is_object()) {
        send_json_response(http::status::bad_request,
            json::value({{"error", "Expected a JSON object"}}));
        return;
    }

    const auto& obj = j.as_object();

    // Extract "type" field (required)
    std::string event_type;
    auto type_it = obj.find("type");
    if (type_it != obj.end() && type_it->value().is_string()) {
        event_type = json::value_to<std::string>(type_it->value());
    } else {
        send_json_response(http::status::bad_request,
            json::value({{"error", "Missing required field: type"}}));
        return;
    }

    // Extract "source" field (optional)
    std::optional<std::string> source;
    auto source_it = obj.find("source");
    if (source_it != obj.end() && source_it->value().is_string()) {
        source = json::value_to<std::string>(source_it->value());
    }

    // Extract "affected" field (optional)
    json::array affected_arr;
    auto affected_it = obj.find("affected");
    if (affected_it != obj.end()) {
        const auto& affected_val = affected_it->value();
        if (affected_val.is_array()) {
            for (const auto& elem : affected_val.as_array()) {
                if (elem.is_string()) {
                    affected_arr.push_back(elem);
                } else if (elem.is_int64()) {
                    affected_arr.push_back(elem.as_int64());
                } else if (elem.is_double()) {
                    affected_arr.push_back(static_cast<int64_t>(elem.as_double()));
                }
            }
        } else if (affected_val.is_string()) {
            boost::json::array single;
            single.push_back(affected_val);
            affected_arr = std::move(single);
        }
    }

    // Extract "tags" field (optional)
    std::vector<std::string> tags;
    auto tags_it = obj.find("tags");
    if (tags_it != obj.end()) {
        const auto& tags_val = tags_it->value();
        if (tags_val.is_array()) {
            for (const auto& elem : tags_val.as_array()) {
                if (elem.is_string()) {
                    tags.push_back(json::value_to<std::string>(elem));
                }
            }
        }
    }

    // Extract "payload" field (optional, defaults to the whole data)
    json::value payload;
    auto payload_it = obj.find("payload");
    if (payload_it != obj.end()) {
        payload = payload_it->value();
    } else {
        // Use the whole object as payload (minus "type")
        boost::json::object payload_obj;
        for (const auto& field : obj) {
            if (std::string(field.key()) != "type") {
                payload_obj[field.key()] = field.value();
            }
        }
        payload = std::move(payload_obj);
    }

    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    double timestamp = std::chrono::duration<double>(
        now.time_since_epoch()).count();

    // Build StoredEventData
    StoredEventData event_data;
    event_data.event_type = event_type;
    event_data.source = source;
    event_data.affected = std::move(affected_arr);
    event_data.tags = std::move(tags);
    event_data.payload = std::move(payload);
    event_data.timestamp = timestamp;

    // Create event in engine and dispatch
    long long event_id = -1;
    if (engine_) {
        event_id = engine_->createEvent(event_data);
        engine_->dispatchEvent(event_data);
        Telemetry::instance().recordPushSuccess();
    }

    json::object resp;
    resp["status"] = json::value("success");
    resp["event_id"] = json::value(static_cast<int64_t>(event_id));
    resp["published"] = json::value(engine_ != nullptr);
    send_json_response(http::status::ok, resp);
}

void Session::handle_pull()
{
    // Parse query parameter: ?offset=N
    int offset = 0;
    std::string target = request_.target();
    size_t pos = target.find('?');
    if (pos != std::string::npos) {
        std::string query = target.substr(pos + 1);
        // Parse offset parameter
        size_t off_pos = query.find("offset=");
        if (off_pos != std::string::npos) {
            try {
                offset = std::stoi(query.substr(off_pos + 7));
            } catch (...) {
                offset = 0;
            }
        }
    }

    // Query events from engine
    json::array events_arr;
    if (engine_) {
        // Use the engine's storage to pull events
        // We'll use a simple approach: return telemetry data as fallback
        // In a full implementation, the engine would expose a pullEvents method
        MLOG_D("pull: offset={} (event retrieval not implemented yet)", offset);
    }

    json::object resp;
    resp["events"] = std::move(events_arr);
    resp["offset"] = json::value(static_cast<int64_t>(offset));
    resp["count"] = json::value(static_cast<int64_t>(events_arr.size()));
    send_json_response(http::status::ok, resp);
}

void Session::handle_stat()
{
    json::object resp;

    // Get telemetry snapshot
    if (engine_) {
        json::object telemetry = Telemetry::instance().toJson();
        resp["telemetry"] = telemetry;
    }

    // Add server-level stats
    json::object stats;
    stats["active_requests"] = json::value(static_cast<int64_t>(server_.active_requests()));
    stats["endpoint_requests"] = json::value(static_cast<int64_t>(server_.stats().requests.load()));
    stats["endpoint_errors"] = json::value(static_cast<int64_t>(server_.stats().errors.load()));
    resp["stats"] = std::move(stats);

    send_json_response(http::status::ok, resp);
}

// ---- HttpServer implementation ----

HttpServer::HttpServer(const HttpServerConfig& config)
    : acceptor_(ioc_), config_(config), stopped_(false)
{
}

HttpServer::~HttpServer()
{
    stop();
}

void HttpServer::set_engine(std::shared_ptr<SignalingEngine> engine)
{
    engine_ = engine;
}

bool HttpServer::start()
{
    if (stopped_) {
        stopped_ = false;
    }

    // Setup acceptor
    tcp::endpoint endpoint(boost::asio::ip::make_address(config_.address), config_.port);
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen(boost::asio::socket_base::max_listen_connections);

    MLOG_D("HTTP server listening on {}:{}", config_.address, config_.port);

    // Start accepting connections
    do_accept();

    // Start worker threads
    for (int i = 0; i < config_.num_threads; ++i) {
        threads_.emplace_back([this]() {
            ioc_.run();
        });
    }

    return true;
}

bool HttpServer::stop()
{
    if (stopped_) return false;
    stopped_ = true;

    // Stop acceptor to unblock async_accept
    boost::system::error_code ec;
    acceptor_.close(ec);

    // Stop IO context
    ioc_.stop();

    // Join threads
    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    threads_.clear();

    MLOG_D("HTTP server stopped");
    return true;
}

void HttpServer::handleEvent(const EventData& event)
{
    // Forward events to all connected clients if needed
    // For HTTP server, events are pushed via /push endpoint
    (void)event;
}

void HttpServer::do_accept()
{
    acceptor_.async_accept(
        [this](boost::beast::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<Session>(std::move(socket), engine_, *this)->start();
            }
            if (!stopped_) {
                do_accept();
            }
        });
}

void HttpServer::increment_active()
{
    active_requests_++;
}

void HttpServer::decrement_active()
{
    active_requests_--;
}

int HttpServer::active_requests() const
{
    return active_requests_.load();
}

json::object HttpServer::get_telemetry_snapshot()
{
    return Telemetry::instance().toJson();
}

} // namespace lunaricorn