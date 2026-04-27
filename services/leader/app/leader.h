// leader.h
#pragma once

#include <string>
#include <map>
#include <chrono>
#include <mutex>
#include <boost/json.hpp>

namespace lunaricorn {

namespace json = boost::json;

class NotReadyException : public std::exception {
public:
    explicit NotReadyException(const std::string& msg) : msg_(msg) {}
    const char* what() const noexcept override { return msg_.c_str(); }
private:
    std::string msg_;
};

class Leader {
public:
    Leader();
    ~Leader() = default;

    void shutdown();

    bool update_node(const std::string& node_name,
                     const std::string& node_type,
                     const std::string& instance_key,
                     const std::string& host,
                     int port);

    json::array get_list();
    bool ready() const;
    json::object detailed_status();
    json::object get_cluster_config();

    std::string get_next_message_id();
    std::string get_next_object_id();

    bool update_node_state(const std::string& node,
                           bool ok,
                           const std::string& msg,
                           const json::object& ex);

    json::array get_node_states();

private:
    struct NodeInfo {
        std::string name;
        std::string type;
        std::string instance_key;
        std::string host;
        int port = 0;
        std::chrono::steady_clock::time_point last_seen;
        bool state_ok = true;
        std::string state_msg = "ok";
        json::object state_ex;
    };

    json::object config_;
    json::object cluster_config_;
    std::map<std::string, NodeInfo> nodes_;
    mutable std::mutex mutex_;

    json::object load_yaml_file(const std::string& path) const;
    std::string generate_uuid7() const;
};

} // namespace lunaricorn