#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>

#include <string>
#include <memory>
#include <thread>
#include <vector>

#include "engine.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

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

    // Route handlers
    void handle_get_blob(const std::string& id, http::response<http::string_body>& res);
    void handle_put_blob(const std::string& id, const std::string& body, http::response<http::string_body>& res);
    void handle_get_meta(const std::string& id, http::response<http::string_body>& res);
    void handle_put_meta(const std::string& id, const std::string& body, http::response<http::string_body>& res);
    void handle_health(http::response<http::string_body>& res);

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
};