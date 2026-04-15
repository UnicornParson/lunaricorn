#include "endpoint.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <chrono>
#include <thread>
#include <boost/asio/post.hpp>
#include <boost/json/src.hpp>
#include <Poco/DateTimeFormatter.h>
#include <lunaricorn.h>

using namespace lunaricorn;
constexpr int ACTIVE_REQUESTS_WARN_LIMIT { 10 };
using namespace boost;
using namespace boost::beast;

namespace lunaricorn {

// Convert MaintenanceLogRecord to JSON
json::object to_json(const MaintenanceLogRecord& record) {
    POINT;
    // Format Poco::DateTime to string "YYYY-MM-DD HH:MM:SS"
    std::string dt_str = Poco::DateTimeFormatter::format(
        record.timestamputc, "%Y-%m-%d %H:%M:%S");

    json::object obj;
    obj["offset"] = record.offset;
    obj["owner"] = record.owner;
    obj["token"] = record.token;
    obj["dt"] = dt_str;
    obj["msg"] = record.msg;
    return obj;
}

// Parse LogMessage from JSON, filling missing datetime
std::optional<LogMessage> parse_log_message(const json::value& v) {
    if (!v.is_object()) {
        return std::nullopt;
    }
    const auto& j = v.as_object();
    try {
        LogMessage msg;
        msg.owner = json::value_to<std::string>(j.at("o"));
        msg.token = json::value_to<std::string>(j.at("t"));
        msg.message = json::value_to<std::string>(j.at("m"));
        msg.type = json::value_to<std::string>(j.at("type"));

        // Trim whitespace (basic)
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\n\r"));
            s.erase(s.find_last_not_of(" \t\n\r") + 1);
        };
        trim(msg.owner);
        trim(msg.token);
        trim(msg.message);
        trim(msg.type);

        if (!msg.valid())
            return std::nullopt;

        if (auto it = j.find("dt"); it != j.end() && it->value().is_string()) {
            msg.datetime = json::value_to<std::string>(it->value());
            trim(*msg.datetime);
        } else {
            msg.datetime = current_time_str();
        }
        return msg;
    } catch (...) {
        return std::nullopt;
    }
}



// Session implementation
Session::Session(tcp::socket socket, std::shared_ptr<PGStorage> storage, LogCollectorServer& server)
    : socket_(std::move(socket)), storage_(storage), server_(server) {}

void Session::start() {
    server_.increment_active();
    do_read();
}

void Session::do_read() {
    auto self = shared_from_this();
    http::async_read(socket_, buffer_, request_,
        [self](beast::error_code ec, std::size_t bytes) {
            self->on_read(ec, bytes);
        });
}

void Session::on_read(beast::error_code ec, std::size_t) {
    if (ec == http::error::end_of_stream) {
        // Client closed connection
        socket_.shutdown(tcp::socket::shutdown_send, ec);
        server_.decrement_active();
        return;
    }
    if (ec) {
        std::cerr << "Read error: " << ec.message() << std::endl;
        server_.decrement_active();
        return;
    }
    process_request();
}

void Session::process_request() {
    // Determine target and method
    auto target = request_.target();
    auto method = request_.method();
    std::cout << "[" << current_time_str() << "] call " << target << std::endl;
    POINT;
    try {
        if (method == http::verb::get && target == "/") {
            POINT;
            handle_root();
            POINT;
        } else if (method == http::verb::get && target == "/health") {
            POINT;
            handle_health();
            POINT;
        } else if (method == http::verb::post && target == "/log") {
            POINT;
            handle_log_post();
            POINT;
        } else if (method == http::verb::get && target.starts_with("/log/pull")) {
            POINT;
            DPRINT("on pull. t: " << target);
            // Check if it's /log/pull or /log/pull-plain or /log/download-plain
            if (target == "/log/pull" || target.find("/log/pull?") == 0) {
                handle_log_pull();
            } else if (target == "/log/pull-plain" || target.find("/log/pull-plain?") == 0) {
                handle_log_pull_plain();
            } else if (target == "/log/download-plain" || target.find("/log/download-plain?") == 0) {
                handle_log_download_plain();
            } else {
                send_json_response(http::status::not_found, {{"error", "Not found"}});
            }
            POINT;
        } else if (method == http::verb::post && target == "/log/batch") {
            POINT;
            handle_log_batch_post();
            POINT;
        } else {
            POINT;
            send_json_response(http::status::not_found, {{"error", "Not found"}});
            POINT;
        }
    } catch (const std::exception& e) {
        POINT;
        std::cerr << "Exception in process_request: " << e.what() << std::endl;
        send_json_response(http::status::internal_server_error,
                           {{"error", "Internal server error"}});
        POINT;
    }
    POINT;
}

void Session::send_response(http::status status, const std::string& content_type,
                            const std::string& body, bool add_cors) 
{
    POINT;
    auto res = std::make_shared<http::response<http::string_body>>(status, request_.version());
    res->set(http::field::server, "LogCollector/1.1");
    res->set(http::field::content_type, content_type);
    res->set(http::field::connection, "close");
    if (add_cors) {
        res->set(http::field::access_control_allow_origin, "*");
    }
    res->body() = body;
    res->prepare_payload();

    auto self = shared_from_this();
    http::async_write(socket_, *res,
        [self, res](beast::error_code ec, std::size_t) {
            self->socket_.shutdown(tcp::socket::shutdown_send, ec);
            self->server_.decrement_active();
        });
    POINT;
}

void Session::send_json_response(http::status status, const json::value& j) {
    POINT;
    send_response(status, "application/json", json::serialize(j));
    POINT;
}

void Session::send_plain_response(http::status status, const std::string& text,
                                   const std::vector<std::pair<http::field, std::string>>& extra_headers) 
{
    POINT;
    auto res = std::make_shared<http::response<http::string_body>>(status, request_.version());
    res->set(http::field::server, "LogCollector/1.1");
    res->set(http::field::content_type, "text/plain");
    res->set(http::field::connection, "close");
    for (const auto& h : extra_headers) {
        res->set(h.first, h.second);
    }
    res->body() = text;
    res->prepare_payload();

    auto self = shared_from_this();
    http::async_write(socket_, *res,
        [self, res](beast::error_code ec, std::size_t) {
            self->socket_.shutdown(tcp::socket::shutdown_send, ec);
            self->server_.decrement_active();
        });
    POINT;
}

template<typename F>
void Session::background_post(F&& f) {
    // Post the task to the I/O context, but detach it from the session.
    // The storage_ shared_ptr ensures the storage lives until task completes.
    boost::asio::post(server_.io_context(),
        [storage = storage_, task = std::forward<F>(f)]() mutable {
            try {
                task(storage);
            } catch (const std::exception& e) {
                std::cerr << "Background task error: " << e.what() << std::endl;
            }
        });
}

void Session::handle_root() 
{
    POINT;
    json::object j;
    j["status"] = "online";
    j["service"] = "Log Collector API";
    // Simulate RabbitMQ status: assume connected if storage is valid
    j["rabbitmq"] = storage_ ? "connected" : "disconnected";
    send_json_response(http::status::ok, j);
}

void Session::handle_health() 
{
    POINT;
    if (storage_) {
        json::object j;
        j["status"] = "online";
        POINT;
        send_json_response(http::status::ok, j);
        POINT;
    } else {
        json::object j;
        j["detail"] = "Internal server error";
        POINT;
        send_json_response(http::status::internal_server_error, j);
        POINT;
    }
    POINT;
}

void Session::handle_log_post() {
    // Parse JSON body
    boost::system::error_code ec;
    json::value j = json::parse(request_.body(), ec);
    if (ec) {
        json::object err;
        err["error"] = "Invalid JSON";
        send_json_response(http::status::bad_request, err);
        return;
    }

    auto msg_opt = parse_log_message(j);
    if (!msg_opt) {
        json::object err;
        err["error"] = "Invalid log message format or empty fields";
        send_json_response(http::status::bad_request, err);
        return;
    }

    // Background task: push to storage
    background_post([msg = *msg_opt](std::shared_ptr<PGStorage> st) {
        st->push(msg.owner, msg.token, msg.message);
        // Note: type is not used in push? Python's publish_message_safe only uses owner, token, message.
        // We'll ignore type for now, as storage doesn't have it.
    });

    // Log reception
    DPRINT("Received log from " << msg_opt->owner << "::" << msg_opt->token << ", type: " << msg_opt->type);

    json::object resp;
    resp["status"] = "success";
    send_json_response(http::status::ok, resp);
}

void Session::handle_log_pull() {
    // Parse offset query parameter
    int offset = 0;
    
    auto target = std::string(request_.target());
    auto pos = target.find('?');
    DPRINT("try pull " << target);
    if (pos != std::string::npos) {
        // crude parsing, assume "?offset=123"
        auto query = target.substr(pos + 1);
        if (query.substr(0, 7) == "offset=") {
            offset = std::stoi(query.substr(7));
            DPRINT("pull. use offset " << offset);
        } else {
            DPRINT("pull. invalid offset [[ " << query << " ]]");
        }
    }

    try {
        auto records = storage_->pull(offset);
        DPRINT("pull. found records " << records.size());
        json::array logs;
        for (const auto& rec : records) {
            logs.push_back(to_json(rec));
        }
        json::object resp;
        resp["logs"] = std::move(logs);
        send_json_response(http::status::ok, resp);
    } catch (const std::exception& e) {
        json::object err;
        DPRINT("pull error " << e.what());
        err["detail"] = std::string("Internal server error: ") + e.what();
        send_json_response(http::status::internal_server_error, err);
    }
}

void Session::handle_log_pull_plain() {
    int offset = 0;
    auto target = std::string(request_.target());
    auto pos = target.find('?');
    if (pos != std::string::npos) {
        auto query = target.substr(pos + 1);
        if (query.substr(0, 7) == "offset=") {
            offset = std::stoi(query.substr(7));
        }
    }

    try {
        auto records = storage_->pull(offset);
        std::ostringstream oss;
        if (records.empty()) {
            oss << "No logs available";
        } else {
            for (const auto& rec : records) {
                // Format: offset[datetime]: msg
                std::string dt_str = Poco::DateTimeFormatter::format(
                    rec.timestamputc, "%Y-%m-%d %H:%M:%S");
                oss << rec.offset << "[" << dt_str << "]: " << rec.msg << "\n";
            }
        }
        std::vector<std::pair<http::field, std::string>> headers;
        headers.emplace_back(http::field::cache_control, "no-cache, no-store, must-revalidate");
        headers.emplace_back(http::field::pragma, "no-cache");
        headers.emplace_back(http::field::expires, "0");
        headers.emplace_back(http::field::vary, "Accept-Encoding");
        send_plain_response(http::status::ok, oss.str(), headers);
    } catch (const std::exception& e) {
        json::object err;
        err["detail"] = std::string("Internal server error: ") + e.what();
        send_json_response(http::status::internal_server_error, err);
    }
}

void Session::handle_log_download_plain() {
    int offset = 0;
    auto target = std::string(request_.target());
    auto pos = target.find('?');
    if (pos != std::string::npos) {
        auto query = target.substr(pos + 1);
        if (query.substr(0, 7) == "offset=") {
            offset = std::stoi(query.substr(7));
        }
    }

    try {
        auto records = storage_->pull(offset);
        std::ostringstream oss;
        if (records.empty()) {
            oss << "No logs available";
        } else {
            for (const auto& rec : records) {
                std::string dt_str = Poco::DateTimeFormatter::format(
                    rec.timestamputc, "%Y-%m-%d %H:%M:%S");
                oss << rec.offset << "[" << dt_str << "]: " << rec.msg << "\n";
            }
        }

        // Generate filename with current UTC time
        auto now = std::chrono::system_clock::now();
        std::time_t now_t = std::chrono::system_clock::to_time_t(now);
        std::tm now_tm = *std::gmtime(&now_t);
        char fname_buf[64];
        std::strftime(fname_buf, sizeof(fname_buf), "maintenance_%Y%m%d_%H%M%S.txt", &now_tm);
        std::string filename = fname_buf;

        std::vector<std::pair<http::field, std::string>> headers;
        headers.emplace_back(http::field::content_disposition,
                             "attachment; filename=\"" + filename + "\"");
        headers.emplace_back(http::field::cache_control, "no-cache, no-store, must-revalidate");
        headers.emplace_back(http::field::pragma, "no-cache");
        headers.emplace_back(http::field::expires, "0");
        headers.emplace_back(http::field::vary, "Accept-Encoding");
        send_plain_response(http::status::ok, oss.str(), headers);
    } catch (const std::exception& e) {
        json::object err;
        err["detail"] = std::string("Internal server error: ") + e.what();
        send_json_response(http::status::internal_server_error, err);
    }
}

void Session::handle_log_batch_post() {
    boost::system::error_code ec;
    json::value j = json::parse(request_.body(), ec);
    if (ec) {
        json::object err;
        err["error"] = "Invalid JSON";
        send_json_response(http::status::bad_request, err);
        return;
    }

    if (!j.is_array()) {
        json::object err;
        err["error"] = "Expected an array";
        send_json_response(http::status::bad_request, err);
        return;
    }

    int success_count = 0;
    for (const auto& item : j.as_array()) {
        auto msg_opt = parse_log_message(item);
        if (msg_opt) {
            background_post([msg = *msg_opt](std::shared_ptr<PGStorage> st) {
                st->push(msg.owner, msg.token, msg.message);
            });
            ++success_count;
        }
    }

    std::cerr << "Received " << success_count << " logs in batch" << std::endl;

    json::object resp;
    resp["status"] = "success";
    resp["message"] = std::to_string(success_count) + " logs accepted and queued";
    resp["count"] = success_count;
    send_json_response(http::status::ok, resp);
}

// LogCollectorServer implementation
LogCollectorServer::LogCollectorServer(std::shared_ptr<PGStorage> storage, const ServerConfig& config)
    : acceptor_(ioc_), storage_(storage), config_(config) {
    // Validate storage connection
    if (!storage_) {
        throw std::runtime_error("Storage is null");
    }
    storage_->testConnection();  // throws if failed

    // Setup acceptor
    tcp::endpoint endpoint(net::ip::make_address(config.address), config.port);
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(net::socket_base::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen(net::socket_base::max_listen_connections);
}

LogCollectorServer::~LogCollectorServer() {
    stop();
}

void LogCollectorServer::run() {
    do_accept();

    // Start worker threads
    for (int i = 0; i < config_.num_threads; ++i) {
        threads_.emplace_back([this]() { ioc_.run(); });
    }

    // Wait for all threads
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
}

void LogCollectorServer::stop() {
    if (stopped_) return;
    stopped_ = true;
    ioc_.stop();
}

void LogCollectorServer::do_accept() {
    acceptor_.async_accept(
        [this](beast::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<Session>(std::move(socket), storage_, *this)->start();
            }
            if (!stopped_) {
                do_accept();
            }
        });
}

void LogCollectorServer::increment_active() {
    active_requests_++;
    if (active_requests_.load() >= ACTIVE_REQUESTS_WARN_LIMIT)
        std::cerr << "Active requests: " << active_requests_.load() << std::endl;
}

void LogCollectorServer::decrement_active() {
    active_requests_--;
    if (active_requests_.load() >= ACTIVE_REQUESTS_WARN_LIMIT)
        std::cerr << "Active requests: " << active_requests_.load() << std::endl;
}

int LogCollectorServer::active_requests() const {
    return active_requests_.load();
}

} // namespace lunaricorn