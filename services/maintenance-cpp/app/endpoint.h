#pragma once

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <atomic>
#include <thread>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>
#include "storage.h"


namespace lunaricorn
{

using tcp = boost::asio::ip::tcp;
// Configuration for the server
struct ServerConfig {
    std::string address = "0.0.0.0";
    unsigned short port = 8000;
    int num_threads = 8;  // matches workers=8 in Python version
};

// Log message structure matching Python's LogMessage
struct LogMessage {
    std::string owner;
    std::string token;
    std::string message;
    std::string type;
    std::optional<std::string> datetime;  // if missing, will be filled with current time

    // Validate that required fields are not empty
    bool valid() const {
        return !owner.empty() && !token.empty() && !message.empty() && !type.empty();
    }
};

// Convert MaintenanceLogRecord to JSON object
boost::json::object to_json(const MaintenanceLogRecord& record);



// Parse LogMessage from JSON, filling missing datetime
std::optional<LogMessage> parse_log_message(const boost::json::value& j);

// Forward declaration
class LogCollectorServer;

// Session handles one HTTP connection
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket, std::shared_ptr<PGStorage> storage, LogCollectorServer& server);
    void start();

private:
    void do_read();
    void on_read(boost::beast::error_code ec, std::size_t bytes_transferred);
    void process_request();
    void send_response(boost::beast::http::status status, const std::string& content_type,
                       const std::string& body, bool add_cors = false);
    void send_json_response(boost::beast::http::status status, const boost::json::value& j);
    void send_plain_response(boost::beast::http::status status, const std::string& text,
                             const std::vector<std::pair<boost::beast::http::field, std::string>>& extra_headers = {});

    // Handlers for specific endpoints
    void handle_root();
    void handle_health();
    void handle_log_post();
    void handle_log_pull();
    void handle_log_pull_plain();
    void handle_log_download_plain();
    void handle_log_batch_post();

    // Helper to run storage operation asynchronously (background task)
    template<typename F>
    void background_post(F&& f);

    tcp::socket socket_;
    boost::beast::flat_buffer buffer_;
    boost::beast::http::request<boost::beast::http::string_body> request_;
    std::shared_ptr<PGStorage> storage_;
    LogCollectorServer& server_;
};

// Main server class
class LogCollectorServer {
public:
    LogCollectorServer(std::shared_ptr<PGStorage> storage, const ServerConfig& config);
    ~LogCollectorServer();

    // Start accepting connections (blocking call, runs I/O context)
    void run();

    // Stop the server
    void stop();

    // Access to I/O context for posting background tasks
    boost::asio::io_context& io_context() { return ioc_; }

    // Track active requests (optional, like semaphore in Python)
    void increment_active();
    void decrement_active();
    int active_requests() const;

private:
    void do_accept();

    boost::asio::io_context ioc_;
    tcp::acceptor acceptor_;
    std::shared_ptr<PGStorage> storage_;
    ServerConfig config_;
    std::vector<std::thread> threads_;
    std::atomic<int> active_requests_{0};
    bool stopped_{false};
};

} // namespace lunaricorn