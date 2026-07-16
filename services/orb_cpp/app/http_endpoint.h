#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>

#include <string>
#include <memory>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

#include "engine.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

struct ApiStats {
    std::atomic<uint64_t> health_count{0};
    std::atomic<uint64_t> get_meta_count{0};
    std::atomic<uint64_t> put_meta_count{0};
    std::atomic<uint64_t> get_blob_count{0};
    std::atomic<uint64_t> put_blob_count{0};
    std::atomic<uint64_t> gen_id_count{0};
    std::atomic<uint64_t> search_count{0};

    std::atomic<uint64_t> bytes_in{0};
    std::atomic<uint64_t> bytes_out{0};

    void reset() {
        health_count = 0;
        get_meta_count = 0;
        put_meta_count = 0;
        get_blob_count = 0;
        put_blob_count = 0;
        gen_id_count = 0;
        search_count = 0;
        bytes_in = 0;
        bytes_out = 0;
    }
};

class HttpEndpoint : public std::enable_shared_from_this<HttpEndpoint>
{
public:
    HttpEndpoint(asio::io_context& ioc, const std::string& host, unsigned short port, Engine& engine);

    void start();
    void stop();

private:
    void do_accept();
    void handle_request(
        http::request<http::string_body>& req,
        http::response<http::string_body>& res);

    // Gzip helpers
    static bool client_accepts_gzip(const http::request<http::string_body>& req);
    static std::string compress_gzip(const std::string& data);
    void apply_gzip_if_needed(const http::request<http::string_body>& req,
                              http::response<http::string_body>& res);

    // Stats timer
    void start_stats_timer();
    void on_stats_timer(beast::error_code ec);

    // Route handlers
    void handle_get_blob(const std::string& id, http::response<http::string_body>& res);
    void handle_put_blob(const std::string& id, const std::string& body, http::response<http::string_body>& res);
    void handle_get_meta(const std::string& id, http::response<http::string_body>& res);
    void handle_put_meta(const std::string& id, const std::string& body, http::response<http::string_body>& res);
    void handle_health(http::response<http::string_body>& res);
    void handle_gen_id(http::response<http::string_body>& res);
    void handle_search(const std::string& query_string, http::response<http::string_body>& res);

    struct Session : public std::enable_shared_from_this<Session>
    {
        Session(tcp::socket&& socket, HttpEndpoint& owner);
        void run();
        void do_read();
        void on_read(beast::error_code ec, size_t bytes_transferred);
        void do_write();

        beast::tcp_stream stream_;
        HttpEndpoint& owner_;
        beast::flat_buffer buffer_;
        http::request<http::string_body> req_;
        http::response<http::string_body> res_;
    };

    asio::io_context& ioc_;
    tcp::acceptor acceptor_;
    Engine& engine_;
    bool running_ = false;

    // Stats
    ApiStats stats_;
    asio::steady_timer stats_timer_;
};