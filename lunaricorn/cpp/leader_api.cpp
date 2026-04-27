#include "leader_api.h"
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/HTTPMessage.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/URI.h>
#include <Poco/Exception.h>
#include <Poco/StreamCopier.h>
#include <sstream>
#include <iostream>
#include "maintenance.h"
namespace lunaricorn
{

LeaderConnector::~LeaderConnector() {
    close();
}

void LeaderConnector::ensure_session() {
    std::lock_guard<std::mutex> lock(session_mutex_);
    if (!session_) {
        Poco::URI uri(base_url_);
        session_ = std::make_unique<Poco::Net::HTTPClientSession>(uri.getHost(), uri.getPort());
        session_->setTimeout(Poco::Timespan(timeout_seconds_, 0));
    }
}

Poco::JSON::Object::Ptr LeaderConnector::make_request(const std::string& method,
                                                      const std::string& endpoint,
                                                      const Poco::JSON::Object::Ptr& data)
{
    std::lock_guard<std::mutex> lock(session_mutex_);
    ensure_session();

    std::string uri_path = endpoint;
    if (!uri_path.empty() && uri_path[0] != '/')
        uri_path = "/" + uri_path;

    Poco::Net::HTTPRequest request(method, uri_path, Poco::Net::HTTPMessage::HTTP_1_1);
    request.setContentType("application/json");
    request.set("Accept", "application/json");

    std::string request_body;
    if (data) {
        std::ostringstream oss;
        Poco::JSON::Stringifier::stringify(data, oss);
        request_body = oss.str();
        request.setContentLength(request_body.length());
    }

    try {
        std::ostream& os = session_->sendRequest(request);
        if (!request_body.empty()) {
            os << request_body;
        }

        Poco::Net::HTTPResponse response;
        std::istream& is = session_->receiveResponse(response);

        if (response.getStatus() != Poco::Net::HTTPResponse::HTTP_OK) {
            MLOG_E("HTTP request failed: {} {} -> {} {}", 
                method, 
                endpoint, 
                static_cast<int>(response.getStatus()),
                response.getReasonForStatus(response.getStatus()));
            throw Poco::Exception("HTTP error: " + std::to_string(response.getStatus()));
        }

        std::string response_body;
        Poco::StreamCopier::copyToString(is, response_body);

        if (response_body.empty()) {
            return new Poco::JSON::Object();
        }

        Poco::JSON::Parser parser;
        Poco::Dynamic::Var result = parser.parse(response_body);
        return result.extract<Poco::JSON::Object::Ptr>();
    }
    catch (const Poco::Exception& e) {
        MLOG_E("Request failed for {} {}: {}", method, endpoint, e.displayText());
        throw;
    }
    catch (const std::exception& e) {
        MLOG_E("Request failed for {} {}: {}", method, endpoint, e.what());
        throw;
    }
}

Poco::JSON::Object::Ptr LeaderConnector::health_check() {
    return make_request(Poco::Net::HTTPRequest::HTTP_GET, "/health");
}

Poco::JSON::Object::Ptr LeaderConnector::get_api_info() {
    return make_request(Poco::Net::HTTPRequest::HTTP_GET, "/");
}

bool LeaderConnector::send_registration_request() {
    std::lock_guard<std::mutex> lock(registered_service_mutex_);
    if (!registered_service_.has_value()) {
        MLOG_W("No service registered for periodic updates");
        return false;
    }

    const auto& svc = *registered_service_;
    Poco::JSON::Object::Ptr data = new Poco::JSON::Object();
    data->set("node_name", svc.node_name);
    data->set("node_type", svc.node_type);
    data->set("instance_key", svc.instance_key);

    if (svc.host.has_value())
        data->set("host", *svc.host);
    if (svc.port.has_value())
        data->set("port", *svc.port);
    if (svc.additional.has_value() && svc.additional.value())
        data->set("additional", svc.additional.value());

    MLOG_D("@@ send _send_registration_request {}", "data");

    try {
        auto response = make_request(Poco::Net::HTTPRequest::HTTP_POST, "/v1/imalive", data);
        MLOG_D("Periodic registration successful: {}", svc.node_name);
        return true;
    }
    catch (...) {
        MLOG_E("Periodic registration failed for {}", svc.node_name);
        return false;
    }
}

void LeaderConnector::registration_timer_worker(std::stop_token stop_token) {
    using namespace std::chrono_literals;
    while (!stop_token.stop_requested()) {
        auto start = std::chrono::steady_clock::now();
        send_registration_request();
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto remaining = std::chrono::seconds(registration_interval_seconds_) - elapsed;
        if (remaining > 0s) {
            std::this_thread::sleep_for(remaining);
        }
    }
    MLOG_D("Registration timer stopped");
}

Poco::JSON::Object::Ptr LeaderConnector::register_service(const std::string& node_name,
                                                          const std::string& node_type,
                                                          const std::string& instance_key,
                                                          const std::optional<std::string>& host,
                                                          const std::optional<int>& port,
                                                          const std::optional<Poco::JSON::Object::Ptr>& additional)
{
    stop_registration_timer();

    {
        std::lock_guard<std::mutex> lock(registered_service_mutex_);
        registered_service_ = RegisteredService{
            node_name,
            node_type,
            instance_key,
            host,
            port,
            additional
        };
    }

    Poco::JSON::Object::Ptr data = new Poco::JSON::Object();
    data->set("node_name", node_name);
    data->set("node_type", node_type);
    data->set("instance_key", instance_key);

    if (host.has_value())
        data->set("host", *host);
    if (port.has_value())
        data->set("port", *port);
    if (additional.has_value() && additional.value())
        data->set("additional", additional.value());

    MLOG_D("Registering service: {} ({})", node_name, node_type);
    auto response = make_request(Poco::Net::HTTPRequest::HTTP_POST, "/v1/imalive", data);

    registration_stop_flag_ = false;
    registration_thread_ = std::jthread([this](std::stop_token st) {
        registration_timer_worker(st);
    });
    MLOG_D("Started periodic registration timer for {}", node_name);

    return response;
}

void LeaderConnector::stop_registration_timer() {
    if (registration_thread_.joinable()) {
        MLOG_D("Stopping registration timer");
        registration_thread_.request_stop();
        registration_thread_.join();
        MLOG_D("Registration timer stopped");
    }
    {
        std::lock_guard<std::mutex> lock(registered_service_mutex_);
        registered_service_.reset();
    }
}

Poco::JSON::Object::Ptr LeaderConnector::list_services() {
    return make_request(Poco::Net::HTTPRequest::HTTP_GET, "/v1/list");
}

Poco::JSON::Object::Ptr LeaderConnector::discover_services(const std::string& query) {
    Poco::JSON::Object::Ptr data = new Poco::JSON::Object();
    data->set("query", query);
    MLOG_D("Discovering services with query: {}", query);
    return make_request(Poco::Net::HTTPRequest::HTTP_POST, "/v1/discover", data);
}

Poco::JSON::Object::Ptr LeaderConnector::get_environment() {
    return make_request(Poco::Net::HTTPRequest::HTTP_GET, "/v1/getenv");
}

Poco::JSON::Object::Ptr LeaderConnector::get_cluster_info() {
    auto response = make_request(Poco::Net::HTTPRequest::HTTP_GET, "/v1/clusterinfo");

    if (response->has("nodes_summary") && response->has("required_nodes"))
        return response;

    Poco::JSON::Object::Ptr result = new Poco::JSON::Object();

    if (response->has("nodes")) {
        result->set("nodes_summary", response->get("nodes"));
    } else if (response->has("nodes_summary")) {
        result->set("nodes_summary", response->get("nodes_summary"));
    } else {
        result->set("nodes_summary", Poco::JSON::Object::Ptr(new Poco::JSON::Object()));
    }

    if (response->has("required")) {
        result->set("required_nodes", response->get("required"));
    } else if (response->has("required_nodes")) {
        result->set("required_nodes", response->get("required_nodes"));
    } else {
        result->set("required_nodes", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    }

    return result;
}

bool LeaderConnector::is_ready() {
    try {
        auto health = health_check();
        if (health->has("status")) {
            return health->getValue<std::string>("status") == "healthy";
        }
        return false;
    }
    catch (...) {
        return false;
    }
}

bool LeaderConnector::wait_for_ready(int timeout_seconds, int check_interval_seconds) {
    using namespace std::chrono;
    auto start = steady_clock::now();
    auto timeout = seconds(timeout_seconds);
    auto interval = seconds(check_interval_seconds);

    while (steady_clock::now() - start < timeout) {
        if (is_ready()) {
            MLOG_D("Leader service is ready");
            return true;
        }
        MLOG_D("Leader service not ready, waiting {} seconds...", check_interval_seconds);
        std::this_thread::sleep_for(interval);
    }
    MLOG_W("Leader service did not become ready within {} seconds", timeout_seconds);
    return false;
}

std::optional<Poco::JSON::Object::Ptr> LeaderConnector::get_service_by_name(const std::string& service_name) {
    try {
        auto services_data = list_services();
        auto services = services_data->getArray("services");
        if (!services) return std::nullopt;

        for (size_t i = 0; i < services->size(); ++i) {
            auto svc = services->getObject(i);
            if (svc && svc->has("name") && svc->getValue<std::string>("name") == service_name) {
                return svc;
            }
        }
        return std::nullopt;
    }
    catch (const std::exception& e) {
        MLOG_E("Failed to get service {}: {}", service_name, e.what());
        return std::nullopt;
    }
}

std::vector<Poco::JSON::Object::Ptr> LeaderConnector::get_services_by_type(const std::string& service_type) {
    std::vector<Poco::JSON::Object::Ptr> result;
    try {
        auto services_data = list_services();
        auto services = services_data->getArray("services");
        if (!services) return result;

        for (size_t i = 0; i < services->size(); ++i) {
            auto svc = services->getObject(i);
            if (svc && svc->has("type") && svc->getValue<std::string>("type") == service_type) {
                result.push_back(svc);
            }
        }
    }
    catch (const std::exception& e) {
        MLOG_E("Failed to get services of type {}: {}", service_type, e.what());
    }
    return result;
}

bool LeaderConnector::ping_service(const std::string& node_name, const std::string& node_type, const std::string& instance_key) {
    try {
        auto response = register_service(node_name, node_type, instance_key);
        return response->optValue<std::string>("status", "") == "received";
    }
    catch (const std::exception& e) {
        MLOG_E("Failed to ping service {}: {}", node_name, e.what());
        return false;
    }
}

void LeaderConnector::close() {
    stop_registration_timer();
    std::lock_guard<std::mutex> lock(session_mutex_);
    if (session_) {
        session_->reset();
        session_.reset();
    }
    MLOG_D("LeaderConnector closed");
}

int64_t LeaderConnector::get_next_message_id() {
    try {
        auto resp = make_request(Poco::Net::HTTPRequest::HTTP_GET, "/v1/utils/get_mid");
        if (resp->has("message_id"))
            return resp->getValue<int64_t>("message_id");
        return 0;
    }
    catch (...) {
        MLOG_E("Failed to get next message id");
        return 0;
    }
}

int64_t LeaderConnector::get_next_object_id() {
    auto resp = make_request(Poco::Net::HTTPRequest::HTTP_GET, "/v1/utils/get_oid");
    return resp->getValue<int64_t>("object_id");
}

std::unique_ptr<LeaderConnector> ConnectorUtils::create_leader_connector(const std::string& base_url) {
    return std::make_unique<LeaderConnector>(base_url);
}

bool ConnectorUtils::quick_health_check(const std::string& base_url) {
    try {
        LeaderConnector conn(base_url);
        return conn.is_ready();
    }
    catch (...) {
        return false;
    }
}

bool ConnectorUtils::test_connection(const std::string& base_url) {
    if (base_url.empty()) return false;
    try {
        Poco::URI uri(base_url);
        Poco::Net::HTTPClientSession session(uri.getHost(), uri.getPort());
        session.setTimeout(Poco::Timespan(5, 0));
        Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, "/", Poco::Net::HTTPMessage::HTTP_1_1);
        session.sendRequest(request);
        Poco::Net::HTTPResponse response;
        session.receiveResponse(response);
        return response.getStatus() == Poco::Net::HTTPResponse::HTTP_OK;
    }
    catch (const Poco::Exception& e) {
        MLOG_E("Failed to connect to {}: {}", base_url, e.displayText());
        return false;
    }
    catch (const std::exception& e) {
        MLOG_E("Failed to connect to {}: {}", base_url, e.what());
        return false;
    }
}

bool ConnectorUtils::wait_connection(const std::string& url, double seconds_timeout) {
    using namespace std::chrono;
    auto start = steady_clock::now();
    while (true) {
        if (test_connection(url)) return true;
        if (duration<double>(steady_clock::now() - start).count() >= seconds_timeout)
            return false;
        std::this_thread::sleep_for(500ms);
    }
}
} // namespace lunaricorn