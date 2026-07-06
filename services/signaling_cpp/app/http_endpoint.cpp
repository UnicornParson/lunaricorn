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
        // Route: GET /v1/list/tags
        else if (method == http::verb::get && target == "/v1/list/tags") {
            handle_list("tags");
        }
        // Route: GET /v1/list/types
        else if (method == http::verb::get && target == "/v1/list/types") {
            handle_list("type");
        }
        // Route: GET /v1/list/affected
        else if (method == http::verb::get && target == "/v1/list/affected") {
            handle_list("affected");
        }
        // Route: GET /v1/list/owners
        else if (method == http::verb::get && target == "/v1/list/owners") {
            handle_list("owner");
        }
        // Route: GET /v1/stat/clients
        else if (method == http::verb::get && target == "/v1/stat/clients") {
            handle_clients();
        }
        // Route: POST /v1/browse
        else if (method == http::verb::post && target == "/v1/browse") {
            handle_browse();
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

void Session::handle_browse()
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

    // Parse optional filters from BrowseRequest
    // event_types: array of strings
    std::vector<std::string> event_types;
    auto et_it = obj.find("event_types");
    if (et_it != obj.end() && et_it->value().is_array()) {
        for (const auto& elem : et_it->value().as_array()) {
            if (elem.is_string()) {
                event_types.push_back(json::value_to<std::string>(elem));
            }
        }
    }

    // sources: array of strings
    std::vector<std::string> sources;
    auto src_it = obj.find("sources");
    if (src_it != obj.end() && src_it->value().is_array()) {
        for (const auto& elem : src_it->value().as_array()) {
            if (elem.is_string()) {
                sources.push_back(json::value_to<std::string>(elem));
            }
        }
    }

    // affected: array (strings or numbers)
    std::vector<std::string> affected;
    auto aff_it = obj.find("affected");
    if (aff_it != obj.end() && aff_it->value().is_array()) {
        for (const auto& elem : aff_it->value().as_array()) {
            if (elem.is_string()) {
                affected.push_back(json::value_to<std::string>(elem));
            } else if (elem.is_int64()) {
                affected.push_back(std::to_string(elem.as_int64()));
            } else if (elem.is_double()) {
                affected.push_back(std::to_string(static_cast<int64_t>(elem.as_double())));
            }
        }
    }

    // tags: array of strings
    std::vector<std::string> tags;
    auto tag_it = obj.find("tags");
    if (tag_it != obj.end() && tag_it->value().is_array()) {
        for (const auto& elem : tag_it->value().as_array()) {
            if (elem.is_string()) {
                tags.push_back(json::value_to<std::string>(elem));
            }
        }
    }

    // timestamp: number (filter events after this timestamp)
    std::optional<double> timestamp;
    auto ts_it = obj.find("timestamp");
    if (ts_it != obj.end()) {
        const auto& ts_val = ts_it->value();
        if (ts_val.is_int64()) {
            timestamp = static_cast<double>(ts_val.as_int64());
        } else if (ts_val.is_double()) {
            timestamp = ts_val.as_double();
        }
    }

    // limit: number (max events to return, 0 = no limit)
    int limit = 0;
    auto lim_it = obj.find("limit");
    if (lim_it != obj.end()) {
        const auto& lim_val = lim_it->value();
        if (lim_val.is_int64()) {
            limit = static_cast<int>(lim_val.as_int64());
        } else if (lim_val.is_double()) {
            limit = static_cast<int>(lim_val.as_double());
        }
    }

    // Call engine->findEvents() with filters
    if (!engine_) {
        send_json_response(http::status::service_unavailable,
            json::value({{"error", "Engine not available"}}));
        return;
    }

    std::vector<StoredEventDataExtended> events =
        engine_->findEvents(timestamp.value_or(0.0),
                            event_types, sources, affected, tags, limit);

    // Build response array
    json::array events_arr;
    for (const auto& ev : events) {
        json::object ev_obj;
        ev_obj["eid"] = json::value(static_cast<int64_t>(ev.eid));
        ev_obj["type"] = json::value(ev.event_type);

        // payload as raw json::value
        ev_obj["payload"] = ev.payload;

        // affected as array
        json::array aff_arr;
        if (ev.affected.is_array()) {
            for (const auto& a : ev.affected.as_array()) {
                aff_arr.push_back(a);
            }
        } else if (ev.affected.is_string()) {
            aff_arr.push_back(ev.affected);
        } else if (ev.affected.is_int64()) {
            aff_arr.push_back(ev.affected.as_int64());
        } else if (ev.affected.is_double()) {
            aff_arr.push_back(ev.affected.as_double());
        }
        ev_obj["affected"] = std::move(aff_arr);

        // tags as array
        json::array tags_arr;
        for (const auto& t : ev.tags) {
            tags_arr.push_back(json::value(t));
        }
        ev_obj["tags"] = std::move(tags_arr);

        // source
        if (ev.source.has_value()) {
            ev_obj["source"] = json::value(ev.source.value());
        } else {
            ev_obj["source"] = json::value(std::string(""));
        }

        // timestamp
        ev_obj["timestamp"] = json::value(static_cast<int64_t>(ev.timestamp));

        events_arr.push_back(std::move(ev_obj));
    }

    json::object resp;
    resp["events"] = std::move(events_arr);
    resp["count"] = json::value(static_cast<int64_t>(events.size()));
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

// ---- Additional route handlers ----

void Session::handle_list(const std::string& field_name)
{
    if (!engine_) {
        send_json_response(http::status::service_unavailable,
            json::value({{"error", "Engine not available"}}));
        return;
    }

    try {
        std::vector<std::string> values = engine_->getUniqueValues(field_name);

        json::array arr;
        for (const auto& v : values) {
            arr.push_back(json::value(v));
        }

        json::object resp;
        resp["field"] = json::value(field_name);
        resp["count"] = json::value(static_cast<int64_t>(values.size()));
        resp["values"] = std::move(arr);

        send_json_response(http::status::ok, resp);
    } catch (const std::exception& e) {
        MLOG_E("handle_list('{}') exception: {}", field_name, e.what());
        send_json_response(http::status::internal_server_error,
            json::value({{"error", std::string("Internal error: ") + e.what()}}));
    }
}

void Session::handle_clients()
{
    json::object resp;

    if (engine_) {
        // Get telemetry for subscriber stats
        json::object telemetry = Telemetry::instance().toJson();
        resp["telemetry"] = telemetry;

        // Add server-level stats
        json::object stats;
        stats["active_requests"] = json::value(static_cast<int64_t>(server_.active_requests()));
        resp["stats"] = std::move(stats);
    } else {
        json::object stats;
        stats["active_requests"] = json::value(static_cast<int64_t>(server_.active_requests()));
        resp["stats"] = std::move(stats);
    }

    send_json_response(http::status::ok, resp);
}

} // namespace lunaricorn
