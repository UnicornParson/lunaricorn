#pragma once

#include "orb_controller.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/strand.hpp>

#include <string>
#include <memory>
#include <functional>
#include <zlib.h>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

/// HTTP client implementation of IOrbController.
/// Connects to an orb_cpp server and provides all API methods.
/// Automatically sends Accept-Encoding: gzip and decompresses gzip responses.
class OrbClient : public IOrbController, public std::enable_shared_from_this<OrbClient>
{
public:
    /// Construct with server URL (e.g. "http://127.0.0.1:8081")
    OrbClient(asio::io_context& ioc, const std::string& server_url);

    ~OrbClient() override;

    // IOrbController interface
    bool health() override;
    std::optional<OrbMetaData> get_meta(const std::string& id) override;
    bool put_meta(const std::string& id, const OrbMetaData& data) override;
    std::optional<json::object> get_blob(const std::string& id) override;
    bool put_blob(const std::string& id, const json::object& data) override;
    std::vector<OrbMetaData> search_by_tags(const std::vector<std::string>& tags) override;
    std::string generate_id() override;

    /// Start periodic health check timer
    void start_health_check();

    /// Stop periodic health check timer
    void stop_health_check();

    /// Check if server is currently considered alive
    bool is_server_alive() const { return server_alive_; }

    /// Set a callback for server status changes (alive -> dead or dead -> alive)
    using StatusCallback = std::function<void(bool alive)>;
    void set_status_callback(StatusCallback cb) { status_cb_ = std::move(cb); }

private:
    /// Internal HTTP request helper
    struct HttpResponse {
        int status = 0;
        std::string body;
    };

    HttpResponse do_request(const std::string& method, const std::string& path,
                            const std::string& body = "");

    /// Decompress gzip data
    static std::string decompress_gzip(const std::string& data);

    /// Parse URL into host and port
    void parse_url(const std::string& url);

    /// Health check timer callback
    void on_health_timer(beast::error_code ec);

    asio::io_context& ioc_;
    std::string host_;
    unsigned short port_ = 8081;
    std::string target_prefix_;  // e.g. "" or "/api/v1"

    bool server_alive_ = false;
    StatusCallback status_cb_;

    asio::steady_timer health_timer_;
    static constexpr int HEALTH_CHECK_INTERVAL_SEC = 10;
};