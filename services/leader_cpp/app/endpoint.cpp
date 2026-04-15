#include "endpoint.h"
#include "leader.h"
#include "stdafx.h"
#include <lunaricorn.h>
#include <string_view>

namespace lunaricorn {

class Endpoint::Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket, std::shared_ptr<Leader> leader);
    void start();

private:
    tcp::socket socket_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    std::shared_ptr<Leader> leader_;

    void read_request();
    void handle_request();
    void send_response(http::response<http::string_body> res);

    static http::response<http::string_body> make_response(http::status status, const json::value& body);
    static std::string current_iso_time();

    http::response<http::string_body> handle_imalive();
    http::response<http::string_body> handle_list_services();
    http::response<http::string_body> handle_discover();
    http::response<http::string_body> handle_test();
    http::response<http::string_body> handle_cluster_info();
    http::response<http::string_body> handle_get_environment();
    http::response<http::string_body> handle_get_mid();
    http::response<http::string_body> handle_get_oid();
    http::response<http::string_body> handle_root();
    http::response<http::string_body> handle_health();
    http::response<http::string_body> handle_api_root();
    http::response<http::string_body> handle_push_node_message();
    http::response<http::string_body> handle_node_states();
    http::response<http::string_body> not_found();
};

namespace {
template <typename T>
T get_optional_value(const json::object& obj, std::string_view key, T fallback) {
    if (const json::value* value = obj.if_contains(key)) {
        try {
            return json::value_to<T>(*value);
        } catch (...) {
        }
    }
    return fallback;
}
} // namespace

Endpoint::Endpoint(net::io_context& ioc, std::shared_ptr<Leader> leader)
    : ioc_(ioc), leader_(std::move(leader)) {}

void Endpoint::run(tcp::endpoint endpoint) {
    acceptor_ = std::make_unique<tcp::acceptor>(ioc_, endpoint);
    do_accept();
}

void Endpoint::do_accept() {
    acceptor_->async_accept(
        [self = shared_from_this()](beast::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<Session>(std::move(socket), self->leader_)->start();
            }
            self->do_accept();
        });
}

// Session implementation

Endpoint::Session::Session(tcp::socket socket, std::shared_ptr<Leader> leader)
    : socket_(std::move(socket)), leader_(std::move(leader)) {}

void Endpoint::Session::start() {
    read_request();
}

void Endpoint::Session::read_request() {
    auto self = shared_from_this();
    http::async_read(socket_, buffer_, req_,
        [self](beast::error_code ec, std::size_t) {
            if (!ec) {
                self->handle_request();
            }
        });
}

void Endpoint::Session::handle_request() {
    http::response<http::string_body> res;

    try {
        if (req_.method() == http::verb::post && req_.target() == "/v1/imalive") {
            res = handle_imalive();
        } else if (req_.method() == http::verb::get && req_.target() == "/v1/list") {
            res = handle_list_services();
        } else if (req_.method() == http::verb::post && req_.target() == "/v1/discover") {
            res = handle_discover();
        } else if (req_.method() == http::verb::get && req_.target() == "/v1/test") {
            res = handle_test();
        } else if (req_.method() == http::verb::get && req_.target() == "/v1/clusterinfo") {
            res = handle_cluster_info();
        } else if (req_.method() == http::verb::get && req_.target() == "/v1/getenv") {
            res = handle_get_environment();
        } else if (req_.method() == http::verb::get && req_.target() == "/v1/utils/get_mid") {
            res = handle_get_mid();
        } else if (req_.method() == http::verb::get && req_.target() == "/v1/utils/get_oid") {
            res = handle_get_oid();
        } else if (req_.method() == http::verb::get && req_.target() == "/") {
            res = handle_root();
        } else if (req_.method() == http::verb::get && req_.target() == "/health") {
            res = handle_health();
        } else if (req_.method() == http::verb::get && req_.target() == "/v1") {
            res = handle_api_root();
        } else if (req_.method() == http::verb::post && req_.target() == "/v1/nodes/push_message") {
            res = handle_push_node_message();
        } else if (req_.method() == http::verb::get && req_.target() == "/v1/nodes/states") {
            res = handle_node_states();
        } else {
            res = not_found();
        }
    } catch (const NotReadyException& e) {
        MLOG_E("Leader not ready: {}", e.what());
        res = make_response(http::status::internal_server_error,
            json::object{{"message", std::string("Leader is not ready to start ") + current_iso_time()}});
    } catch (const std::exception& e) {
        MLOG_E("Unhandled exception: {}", e.what());
        res = make_response(http::status::internal_server_error,
            json::object{{"message", std::string("Internal server error: ") + e.what()}});
    }

    send_response(std::move(res));
}

void Endpoint::Session::send_response(http::response<http::string_body> res) {
    auto self = shared_from_this();
    auto response = std::make_shared<http::response<http::string_body>>(std::move(res));
    response->prepare_payload();
    http::async_write(socket_, *response,
        [self, response](beast::error_code ec, std::size_t) {
            self->socket_.shutdown(tcp::socket::shutdown_send, ec);
        });
}

http::response<http::string_body> Endpoint::Session::make_response(http::status status, const json::value& body) {
    http::response<http::string_body> res{status, 11};
    res.set(http::field::content_type, "application/json");
    res.body() = json::serialize(body);
    return res;
}

std::string Endpoint::Session::current_iso_time() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::gmtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

http::response<http::string_body> Endpoint::Session::handle_imalive() {
    MLOG_D("Received im_alive notification");
    if (!leader_) {
        MLOG_E("Leader is not initialized");
        return make_response(http::status::internal_server_error,
            json::object{{"message", "Leader is not initialized"}});
    }

    try {
        json::value data_value = json::parse(req_.body());
        if (!data_value.is_object()) {
            return make_response(http::status::bad_request,
                json::object{{"message", "Invalid JSON"}});
        }
        const json::object& data = data_value.as_object();
        if (!data.if_contains("node_name") || !data.at("node_name").is_string()) {
            MLOG_E("Invalid or missing node_name");
            return make_response(http::status::internal_server_error,
                json::object{{"message", "Invalid or missing node_name"}});
        }
        if (!data.if_contains("node_type") || !data.at("node_type").is_string()) {
            MLOG_E("Invalid or missing node_type");
            return make_response(http::status::internal_server_error,
                json::object{{"message", "Invalid or missing node_type"}});
        }
        if (!data.if_contains("instance_key") || !data.at("instance_key").is_string()) {
            MLOG_E("Invalid or missing instance_key");
            return make_response(http::status::internal_server_error,
                json::object{{"message", "Invalid or missing instance_key"}});
        }

        std::string node_name = json::value_to<std::string>(data.at("node_name"));
        std::string node_type = json::value_to<std::string>(data.at("node_type"));
        std::string instance_key = json::value_to<std::string>(data.at("instance_key"));
        std::string host = get_optional_value<std::string>(data, "host", "");
        int port = get_optional_value<int>(data, "port", 0);

        bool rc = leader_->update_node(node_name, node_type, instance_key, host, port);
        if (rc) {
            return make_response(http::status::ok, json::object{{"status", "received"}});
        } else {
            return make_response(http::status::internal_server_error,
                json::object{{"message", "Failed to update node"}});
        }
    } catch (const std::exception& e) {
        MLOG_E("JSON parse error: {}", e.what());
        return make_response(http::status::bad_request,
            json::object{{"message", "Invalid JSON"}});
    }
}

http::response<http::string_body> Endpoint::Session::handle_list_services() {
    MLOG_D("Received request to list services");
    if (!leader_) {
        MLOG_E("Leader is not initialized");
        return make_response(http::status::internal_server_error,
            json::object{{"message", "Leader is not initialized"}});
    }

    auto services = leader_->get_list();
    json::object response = {
        {"services", services},
        {"total_count", services.size()},
        {"timestamp", current_iso_time()}
    };
    return make_response(http::status::ok, response);
}

http::response<http::string_body> Endpoint::Session::handle_discover() {
    MLOG_D("Received discovery request");
    json::value data_value = json::parse(req_.body());
    const json::object* data = data_value.if_object();
    std::string query = data ? get_optional_value<std::string>(*data, "query", "") : "";
    json::object response = {
        {"query", query},
        {"results", json::array{}},
        {"total_count", 0},
        {"timestamp", current_iso_time()}
    };
    return make_response(http::status::ok, response);
}

http::response<http::string_body> Endpoint::Session::handle_test() {
    return make_response(http::status::ok, json::object{{"message", "Hello, World!"}});
}

http::response<http::string_body> Endpoint::Session::handle_cluster_info() {
    MLOG_D("Received request for cluster information");
    if (!leader_ || !leader_->ready()) {
        MLOG_E("Leader service is initializing");
        return make_response(http::status::service_unavailable,
            json::object{{"message", "Leader service is initializing"}});
    }
    auto info = leader_->detailed_status();
    return make_response(http::status::ok, info);
}

http::response<http::string_body> Endpoint::Session::handle_get_environment() {
    MLOG_D("Received request for environment information");
    if (!leader_ || !leader_->ready()) {
        MLOG_E("Leader is not ready");
        return make_response(http::status::internal_server_error,
            json::object{{"message", "Leader is not ready to start"}});
    }
    try {
        auto cluster_config = leader_->get_cluster_config();
        json::object response = {
            {"cfg", cluster_config},
            {"core", "1.0.0"},
            {"timestamp", current_iso_time()}
        };
        return make_response(http::status::ok, response);
    } catch (const std::exception& e) {
        MLOG_E("Error getting cluster configuration: {}", e.what());
        return make_response(http::status::internal_server_error,
            json::object{{"message", "Error getting cluster configuration"}});
    }
}

http::response<http::string_body> Endpoint::Session::handle_get_mid() {
    MLOG_D("Received request for next message id");
    if (!leader_) {
        MLOG_E("Leader is not ready");
        return make_response(http::status::internal_server_error,
            json::object{{"message", "Leader is not ready to start"}});
    }
    auto mid = leader_->get_next_message_id();
    return make_response(http::status::ok, json::object{{"mid", mid}});
}

http::response<http::string_body> Endpoint::Session::handle_get_oid() {
    MLOG_D("Received request for next object id");
    if (!leader_) {
        MLOG_E("Leader is not ready");
        return make_response(http::status::internal_server_error,
            json::object{{"message", "Leader is not ready to start"}});
    }
    auto oid = leader_->get_next_object_id();
    return make_response(http::status::ok, json::object{{"oid", oid}});
}

http::response<http::string_body> Endpoint::Session::handle_root() {
    MLOG_D("Root endpoint accessed");
    json::object response = {
        {"message", "Leader API - Service Discovery and Health Monitoring"},
        {"version", "1.0.0"},
        {"docs", "/docs"},
        {"status", "healthy"}
    };
    return make_response(http::status::ok, response);
}

http::response<http::string_body> Endpoint::Session::handle_health() {
    MLOG_D("Health check endpoint accessed");
    return make_response(http::status::ok,
        json::object{{"status", "healthy"}, {"timestamp", current_iso_time()}});
}

http::response<http::string_body> Endpoint::Session::handle_api_root() {
    MLOG_D("API root endpoint accessed");
    json::object endpoints = {
        {"imalive", "/v1/imalive"},
        {"list", "/v1/list"},
        {"discover", "/v1/discover"},
        {"getenv", "/v1/getenv"},
        {"clusterinfo", "/v1/clusterinfo"},
        {"utils", {
            {"get_mid", "/v1/utils/get_mid"},
            {"get_oid", "/v1/utils/get_oid"}
        }}
    };
    json::object response = {
        {"message", "Leader API v1"},
        {"version", "1.0.0"},
        {"endpoints", endpoints},
        {"status", "healthy"}
    };
    return make_response(http::status::ok, response);
}

http::response<http::string_body> Endpoint::Session::handle_push_node_message() {
    MLOG_D("Received push_message request");
    if (!leader_) {
        MLOG_E("Leader is not initialized");
        return make_response(http::status::internal_server_error,
            json::object{{"message", "Leader is not initialized"}});
    }

    try {
        json::value data_value = json::parse(req_.body());
        if (!data_value.is_object()) {
            return make_response(http::status::bad_request,
                json::object{{"message", "Invalid JSON"}});
        }
        const json::object& data = data_value.as_object();
        std::string node = get_optional_value<std::string>(data, "node", "");
        bool ok = get_optional_value<bool>(data, "ok", true);
        std::string msg = get_optional_value<std::string>(data, "msg", "ok");
        json::object ex = get_optional_value<json::object>(data, "ex", json::object{});

        if (node.empty()) {
            MLOG_E("Missing node field");
            return make_response(http::status::bad_request,
                json::object{{"message", "Missing node field"}});
        }

        bool rc = leader_->update_node_state(node, ok, msg, ex);
        if (rc) {
            return make_response(http::status::ok, json::object{{"status", "updated"}});
        } else {
            return make_response(http::status::internal_server_error,
                json::object{{"message", "Failed to update node state"}});
        }
    } catch (const std::exception& e) {
        MLOG_E("JSON parse error: {}", e.what());
        return make_response(http::status::bad_request,
            json::object{{"message", "Invalid JSON"}});
    }
}

http::response<http::string_body> Endpoint::Session::handle_node_states() {
    MLOG_D("Received request to list node states");
    if (!leader_) {
        MLOG_E("Leader is not initialized");
        return make_response(http::status::internal_server_error,
            json::object{{"message", "Leader is not initialized"}});
    }

    auto states = leader_->get_node_states();
    json::object response = {
        {"states", states},
        {"total_count", states.size()},
        {"timestamp", current_iso_time()}
    };
    return make_response(http::status::ok, response);
}

http::response<http::string_body> Endpoint::Session::not_found() {
    return make_response(http::status::not_found,
        json::object{{"message", "Endpoint not found"}});
}

} // namespace lunaricorn