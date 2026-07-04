#pragma once

#include "endpoint.h"
#include "signaling_engine.h"
#include "telemetry.h"
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <atomic>
#include <thread>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>

namespace lunaricorn
{

using tcp = boost::asio::ip::tcp;
namespace http = boost::beast::http;
namespace json = boost::json;

// Configuration for the HTTP server
struct HttpServerConfig {
    std::string address = "0.0.0.0";
    unsigned short port = 8081;
    int num_threads = 1;
};

// Forward declaration
class HttpServer;

// Session handles one HTTP connection
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket, std::shared_ptr<SignalingEngine> engine, HttpServer& server);
    ~Session();
    void start();

private:
    void do_read();
    void on_read(boost::beast::error_code ec, std::size_t bytes_transferred);
    void process_request();

    void send_response(http::status status, const std::string& content_type,
                       const std::string& body);
    void send_json_response(http::status status, const json::value& j);

    // Handlers for specific endpoints
    void handle_root();
    void handle_health();
    void handle_push();
    void handle_pull();
    void handle_stat();

    tcp::socket socket_;
    boost::beast::flat_buffer buffer_;
    http::request<http::string_body> request_;
    std::shared_ptr<SignalingEngine> engine_;
    HttpServer& server_;
    std::atomic<int64_t> obj_counter_{0};
};

// Main HTTP server class
class HttpServer : public Endpoint {
public:
    HttpServer(const HttpServerConfig& config);
    ~HttpServer();

    void set_engine(std::shared_ptr<SignalingEngine> engine);

    bool start() override;
    bool stop() override;
    void handleEvent(const EventData& event) override;

    boost::asio::io_context& io_context() { return ioc_; }

    // Track active requests
    void increment_active();
    void decrement_active();
    int active_requests() const;

    // Get telemetry snapshot
    json::object get_telemetry_snapshot();

private:
    void do_accept();

    boost::asio::io_context ioc_;
    tcp::acceptor acceptor_;
    HttpServerConfig config_;
    std::vector<std::thread> threads_;
    std::atomic<int> active_requests_{0};
    bool stopped_{false};
    std::shared_ptr<SignalingEngine> engine_;
};

} // namespace lunaricorn