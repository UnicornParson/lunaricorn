// leader_connector.h
#pragma once
#include "stdafx.h"
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/URI.h>
#include "maintenance.h"


class LeaderConnector {
public:
    explicit LeaderConnector(const std::string& base_url = "http://localhost:8001", int timeout_seconds = 30);
    ~LeaderConnector();

    LeaderConnector(const LeaderConnector&) = delete;
    LeaderConnector& operator=(const LeaderConnector&) = delete;
    LeaderConnector(LeaderConnector&&) = delete;
    LeaderConnector& operator=(LeaderConnector&&) = delete;

    Poco::JSON::Object::Ptr health_check();
    Poco::JSON::Object::Ptr get_api_info();
    Poco::JSON::Object::Ptr register_service(const std::string& node_name,
                                             const std::string& node_type,
                                             const std::string& instance_key,
                                             const std::optional<std::string>& host = std::nullopt,
                                             const std::optional<int>& port = std::nullopt,
                                             const std::optional<Poco::JSON::Object::Ptr>& additional = std::nullopt);
    void stop_registration_timer();
    Poco::JSON::Object::Ptr list_services();
    Poco::JSON::Object::Ptr discover_services(const std::string& query);
    Poco::JSON::Object::Ptr get_environment();
    Poco::JSON::Object::Ptr get_cluster_info();
    bool is_ready();
    bool wait_for_ready(int timeout_seconds = 60, int check_interval_seconds = 5);
    std::optional<Poco::JSON::Object::Ptr> get_service_by_name(const std::string& service_name);
    std::vector<Poco::JSON::Object::Ptr> get_services_by_type(const std::string& service_type);
    bool ping_service(const std::string& node_name, const std::string& node_type, const std::string& instance_key);
    void close();
    int64_t get_next_message_id();
    int64_t get_next_object_id();

private:
    struct RegisteredService {
        std::string node_name;
        std::string node_type;
        std::string instance_key;
        std::optional<std::string> host;
        std::optional<int> port;
        std::optional<Poco::JSON::Object::Ptr> additional;
    };

    std::string base_url_;
    int timeout_seconds_;
    std::unique_ptr<Poco::Net::HTTPClientSession> session_;
    std::mutex session_mutex_;

    std::jthread registration_thread_;
    std::atomic<bool> registration_stop_flag_{false};
    std::optional<RegisteredService> registered_service_;
    std::mutex registered_service_mutex_;
    int registration_interval_seconds_ = 30;

    Poco::JSON::Object::Ptr make_request(const std::string& method,
                                         const std::string& endpoint,
                                         const Poco::JSON::Object::Ptr& data = nullptr);
    bool send_registration_request();
    void registration_timer_worker(std::stop_token stop_token);
    void ensure_session();
};

class ConnectorUtils {
public:
    static std::unique_ptr<LeaderConnector> create_leader_connector(const std::string& base_url = "http://localhost:8001");
    static bool quick_health_check(const std::string& base_url = "http://localhost:8001");
    static bool test_connection(const std::string& base_url);
    static bool wait_connection(const std::string& url, double seconds_timeout);
};