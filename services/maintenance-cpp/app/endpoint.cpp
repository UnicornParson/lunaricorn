#include "endpoint.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <chrono>
#include <thread>
#include <boost/asio/post.hpp>

// Helper: current time as string "YYYY-MM-DD HH:MM:SS,mmm"
static std::string current_time_str() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << ','
        << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

// Convert MaintenanceLogRecord to JSON
nlohmann::json to_json(const MaintenanceLogRecord& record) {
    // Assuming timestamputc is of type DateTime that can be converted to string
    // For simplicity, we'll assume it has a .str() method or we convert via time_t
    std::time_t tt = record.timestamputc; // or appropriate conversion
    std::tm tm = *std::gmtime(&tt);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    std::string dt_str = oss.str();

    return nlohmann::json{
        {"offset", record.offset},
        {"owner", record.owner},
        {"token", record.token},
        {"dt", dt_str},
        {"msg", record.msg}
    };
}

// Parse LogMessage from JSON, filling missing datetime
std::optional<LogMessage> parse_log_message(const nlohmann::json& j) {
    try {
        LogMessage msg;
        msg.owner = j.at("o").get<std::string>();
        msg.token = j.at("t").get<std::string>();
        msg.message = j.at("m").get<std::string>();
        msg.type = j.at("type").get<std::string>();

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

        if (j.contains("dt") && !j["dt"].is_null()) {
            msg.datetime = j["dt"].get<std::string>();
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

    try {
        if (method == http::verb::get && target == "/") {
            handle_root();
        } else if (method == http::verb::get && target == "/health") {
            handle_health();
        } else if (method == http::verb::post && target == "/log") {
            handle_log_post();
        } else if (method == http::verb::get && target.starts_with("/log/pull")) {
            // Check if it's /log/pull or /log/pull-plain or /log/download-plain
            if (target == "/log/pull") {
                handle_log_pull();
            } else if (target == "/log/pull-plain") {
                handle_log_pull_plain();
            } else if (target == "/log/download-plain") {
                handle_log_download_plain();
            } else {
                send_json_response(http::status::not_found, {{"error", "Not found"}});
            }
        } else if (method == http::verb::post && target == "/log/batch") {
            handle_log_batch_post();
        } else {
            send_json_response(http::status::not_found, {{"error", "Not found"}});
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception in process_request: " << e.what() << std::endl;
        send_json_response(http::status::internal_server_error,
                           {{"error", "Internal server error"}});
    }
}

void Session::send_response(http::status status, const std::string& content_type,
                            const std::string& body, bool add_cors) {
    http::response<http::string_body> res{status, request_.version()};
    res.set(http::field::server, "LogCollector/1.1");
    res.set(http::field::content_type, content_type);
    if (add_cors) {
        res.set(http::field::access_control_allow_origin, "*");
    }
    res.body() = body;
    res.prepare_payload();

    auto self = shared_from_this();
    http::async_write(socket_, res,
        [self](beast::error_code ec, std::size_t) {
            self->socket_.shutdown(tcp::socket::shutdown_send, ec);
            self->server_.decrement_active();
        });
}

void Session::send_json_response(http::status status, const nlohmann::json& j) {
    send_response(status, "application/json", j.dump());
}

void Session::send_plain_response(http::status status, const std::string& text,
                                   const std::vector<http::field>& extra_headers) {
    http::response<http::string_body> res{status, request_.version()};
    res.set(http::field::server, "LogCollector/1.1");
    res.set(http::field::content_type, "text/plain");
    for (const auto& h : extra_headers) {
        res.set(h.name(), h.value());
    }
    res.body() = text;
    res.prepare_payload();

    auto self = shared_from_this();
    http::async_write(socket_, res,
        [self](beast::error_code ec, std::size_t) {
            self->socket_.shutdown(tcp::socket::shutdown_send, ec);
            self->server_.decrement_active();
        });
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

void Session::handle_root() {
    nlohmann::json j;
    j["status"] = "online";
    j["service"] = "Log Collector API";
    // Simulate RabbitMQ status: assume connected if storage is valid
    j["rabbitmq"] = storage_ ? "connected" : "disconnected";
    send_json_response(http::status::ok, j);
}

void Session::handle_health() {
    if (storage_) {
        send_json_response(http::status::ok, {{"status", "online"}});
    } else {
        send_json_response(http::status::internal_server_error,
                           {{"detail", "Internal server error"}});
    }
}

void Session::handle_log_post() {
    // Parse JSON body
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(request_.body());
    } catch (...) {
        send_json_response(http::status::bad_request, {{"error", "Invalid JSON"}});
        return;
    }

    auto msg_opt = parse_log_message(j);
    if (!msg_opt) {
        send_json_response(http::status::bad_request,
                           {{"error", "Invalid log message format or empty fields"}});
        return;
    }

    // Background task: push to storage
    background_post([msg = *msg_opt](std::shared_ptr<PGStorage> st) {
        st->push(msg.owner, msg.token, msg.message);
        // Note: type is not used in push? Python's publish_message_safe only uses owner, token, message.
        // We'll ignore type for now, as storage doesn't have it.
    });

    // Log reception
    std::cerr << "Received log from " << msg_opt->owner << "::" << msg_opt->token
              << ", type: " << msg_opt->type << std::endl;

    send_json_response(http::status::ok, {{"status", "success"}});
}

void Session::handle_log_pull() {
    // Parse offset query parameter
    int offset = 0;
    auto target = request_.target().to_string();
    auto pos = target.find('?');
    if (pos != std::string::npos) {
        // crude parsing, assume "?offset=123"
        auto query = target.substr(pos + 1);
        if (query.substr(0, 7) == "offset=") {
            offset = std::stoi(query.substr(7));
        }
    }

    try {
        auto records = storage_->pull(offset);
        nlohmann::json logs = nlohmann::json::array();
        for (const auto& rec : records) {
            logs.push_back(to_json(rec));
        }
        send_json_response(http::status::ok, {{"logs", logs}});
    } catch (const std::exception& e) {
        send_json_response(http::status::internal_server_error,
                           {{"detail", std::string("Internal server error: ") + e.what()}});
    }
}

void Session::handle_log_pull_plain() {
    int offset = 0;
    auto target = request_.target().to_string();
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
                std::time_t tt = rec.timestamputc;
                std::tm tm = *std::gmtime(&tt);
                char dt_buf[64];
                std::strftime(dt_buf, sizeof(dt_buf), "%Y-%m-%d %H:%M:%S", &tm);
                oss << rec.offset << "[" << dt_buf << "]: " << rec.msg << "\n";
            }
        }
        std::vector<http::field> headers;
        headers.emplace_back(http::field::cache_control, "no-cache, no-store, must-revalidate");
        headers.emplace_back(http::field::pragma, "no-cache");
        headers.emplace_back(http::field::expires, "0");
        headers.emplace_back(http::field::vary, "Accept-Encoding");
        send_plain_response(http::status::ok, oss.str(), headers);
    } catch (const std::exception& e) {
        send_json_response(http::status::internal_server_error,
                           {{"detail", std::string("Internal server error: ") + e.what()}});
    }
}

void Session::handle_log_download_plain() {
    int offset = 0;
    auto target = request_.target().to_string();
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
                std::time_t tt = rec.timestamputc;
                std::tm tm = *std::gmtime(&tt);
                char dt_buf[64];
                std::strftime(dt_buf, sizeof(dt_buf), "%Y-%m-%d %H:%M:%S", &tm);
                oss << rec.offset << "[" << dt_buf << "]: " << rec.msg << "\n";
            }
        }

        // Generate filename with current UTC time
        auto now = std::chrono::system_clock::now();
        std::time_t now_t = std::chrono::system_clock::to_time_t(now);
        std::tm now_tm = *std::gmtime(&now_t);
        char fname_buf[64];
        std::strftime(fname_buf, sizeof(fname_buf), "maintenance_%Y%m%d_%H%M%S.txt", &now_tm);
        std::string filename = fname_buf;

        std::vector<http::field> headers;
        headers.emplace_back(http::field::content_disposition,
                             "attachment; filename=\"" + filename + "\"");
        headers.emplace_back(http::field::cache_control, "no-cache, no-store, must-revalidate");
        headers.emplace_back(http::field::pragma, "no-cache");
        headers.emplace_back(http::field::expires, "0");
        headers.emplace_back(http::field::vary, "Accept-Encoding");
        send_plain_response(http::status::ok, oss.str(), headers);
    } catch (const std::exception& e) {
        send_json_response(http::status::internal_server_error,
                           {{"detail", std::string("Internal server error: ") + e.what()}});
    }
}

void Session::handle_log_batch_post() {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(request_.body());
    } catch (...) {
        send_json_response(http::status::bad_request, {{"error", "Invalid JSON"}});
        return;
    }

    if (!j.is_array()) {
        send_json_response(http::status::bad_request, {{"error", "Expected an array"}});
        return;
    }

    int success_count = 0;
    for (const auto& item : j) {
        auto msg_opt = parse_log_message(item);
        if (msg_opt) {
            background_post([msg = *msg_opt](std::shared_ptr<PGStorage> st) {
                st->push(msg.owner, msg.token, msg.message);
            });
            ++success_count;
        }
    }

    std::cerr << "Received " << success_count << " logs in batch" << std::endl;

    nlohmann::json resp;
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
    std::cerr << "Active requests: " << active_requests_.load() << std::endl;
}

void LogCollectorServer::decrement_active() {
    active_requests_--;
    std::cerr << "Active requests: " << active_requests_.load() << std::endl;
}

int LogCollectorServer::active_requests() const {
    return active_requests_;
}