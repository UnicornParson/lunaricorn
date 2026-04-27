// leader.cpp
#include "leader.h"

#include <lunaricorn.h>
#include <fstream>
#include <sstream>
#include <random>
#include <iomanip>
#include <charconv>
#include <yaml-cpp/yaml.h>  // Requires yaml-cpp library

namespace lunaricorn {

namespace {
    int parse_alive_timeout(const json::object& config, int fallback = 10) {
        auto it = config.find("discover");
        if (it == config.end() || !it->value().is_object()) {
            return fallback;
        }

        const auto& discover_obj = it->value().as_object();
        auto timeout_it = discover_obj.find("alive_timeout");
        if (timeout_it == discover_obj.end()) {
            return fallback;
        }

        const json::value& timeout_value = timeout_it->value();
        if (timeout_value.is_int64()) {
            return static_cast<int>(timeout_value.as_int64());
        }
        if (timeout_value.is_uint64()) {
            return static_cast<int>(timeout_value.as_uint64());
        }
        if (timeout_value.is_double()) {
            return static_cast<int>(timeout_value.as_double());
        }

        if (timeout_value.is_string()) {
            const std::string raw = timeout_value.as_string().c_str();
            int parsed = fallback;
            auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), parsed);
            if (ec == std::errc() && ptr == raw.data() + raw.size()) {
                return parsed;
            }
            MLOG_W("Invalid alive_timeout value '{}', using default {}", raw, fallback);
            return fallback;
        }

        MLOG_W("Invalid alive_timeout type, using default {}", fallback);
        return fallback;
    }

    json::value yaml_to_json(const YAML::Node& node) {
        if (node.IsMap()) {
            json::object obj;
            for (const auto& it : node) {
                obj[it.first.as<std::string>()] = yaml_to_json(it.second);
            }
            return obj;
        } else if (node.IsSequence()) {
            json::array arr;
            for (const auto& it : node) {
                arr.push_back(yaml_to_json(it));
            }
            return arr;
        } else if (node.IsScalar()) {
            return boost::json::value(node.as<std::string>());
        }
        return nullptr;
    }

    json::object parse_yaml_file(const std::string& path) {
        try {
            YAML::Node root = YAML::LoadFile(path);
            json::value result = yaml_to_json(root);
            if (result.is_object()) {
                return result.as_object();
            }
            return json::object{{"value", result}};
        } catch (const std::exception& e) {
            MLOG_E("Failed to load YAML file {}: {}", path, e.what());
            throw;
        }
    }
} // anonymous namespace

Leader::Leader() {
    config_ = parse_yaml_file("/opt/lunaricorn/leader_data/leader_config.yaml");
    cluster_config_ = parse_yaml_file("/opt/lunaricorn/leader_data/cluster_config.yaml");
    MLOG_D("Leader initialized with in-memory storage");
}

void Leader::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    nodes_.clear();
    MLOG_D("Leader shutdown complete");
}

bool Leader::update_node(const std::string& node_name,
                         const std::string& node_type,
                         const std::string& instance_key,
                         const std::string& host,
                         int port) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& info = nodes_[node_name];
    info.name = node_name;
    info.type = node_type;
    info.instance_key = instance_key;
    info.host = host;
    info.port = port;
    info.last_seen = std::chrono::steady_clock::now();
    return true;
}

json::array Leader::get_list() {
    if (!ready()) {
        throw NotReadyException("Leader is not ready to start");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    int alive_timeout = parse_alive_timeout(config_);

    auto now = std::chrono::steady_clock::now();
    json::array result;
    for (const auto& [name, info] : nodes_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - info.last_seen).count();
        if (elapsed <= alive_timeout) {
            json::object node;
            node["name"] = info.name;
            node["type"] = info.type;
            node["instance_key"] = info.instance_key;
            node["host"] = info.host;
            node["port"] = info.port;
            result.push_back(node);
        }
    }
    return result;
}

bool Leader::ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int alive_timeout = parse_alive_timeout(config_);

    // Get required nodes from config
    std::set<std::string> required;
    if (auto it = config_.find("discover"); it != config_.end()) {
        if (auto req_it = it->value().as_object().find("required_nodes"); req_it != it->value().as_object().end()) {
            if (req_it->value().is_array()) {
                for (const auto& v : req_it->value().as_array()) {
                    required.insert(boost::json::value_to<std::string>(v));
                }
            }
        }
    }

    auto now = std::chrono::steady_clock::now();
    for (const auto& req : required) {
        auto node_it = nodes_.find(req);
        if (node_it == nodes_.end()) {
            MLOG_W("Missing required node: {}", req);
            return false;
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - node_it->second.last_seen).count();
        if (elapsed > alive_timeout) {
            MLOG_W("Required node {} is not alive (last seen {}s ago)", req, elapsed);
            return false;
        }
    }
    return true;
}

json::object Leader::detailed_status() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::set<std::string> required;
    if (auto it = config_.find("discover"); it != config_.end() && it->value().is_object()) {
        const auto& discover_obj = it->value().as_object(); // may throw if not object – handle separately
        if (auto req_it = discover_obj.find("required_nodes"); req_it != discover_obj.end()) {
            if (req_it->value().is_array()) {
                for (const auto& v : req_it->value().as_array()) {
                    if (v.is_string()) {
                        required.insert(v.as_string().c_str());
                    } else {
                        MLOG_W("Required node entry is not a string, skipping");
                    }
                }
            }
        }
    }

    json::object summary;
    for (const auto& req : required) {
        summary[req] = "off";
    }

    int alive_timeout = parse_alive_timeout(config_);

    auto now = std::chrono::steady_clock::now();
    for (const auto& [name, info] : nodes_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - info.last_seen).count();
        if (elapsed <= alive_timeout) {
            summary[name] = "on";
        }
    }

    json::object result;
    result["nodes_summary"] = summary;
    result["required_nodes"] = json::array(required.begin(), required.end());
    return result;
}

json::object Leader::get_cluster_config() {
    return cluster_config_;
}

std::string Leader::get_next_message_id()
{
    static std::atomic<uint64_t> counter{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const uint64_t nt = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    return std::to_string(nt) + "_" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
}

std::string Leader::get_next_object_id() {
    return generate_uuid7();
}

bool Leader::update_node_state(const std::string& node,
                               bool ok,
                               const std::string& msg,
                               const json::object& ex) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(node);
    if (it == nodes_.end()) {
        return false;
    }
    it->second.state_ok = ok;
    it->second.state_msg = msg;
    it->second.state_ex = ex;
    it->second.last_seen = std::chrono::steady_clock::now();
    return true;
}

json::array Leader::get_node_states() {
    std::lock_guard<std::mutex> lock(mutex_);
    json::array result;
    for (const auto& [name, info] : nodes_) {
        json::object entry;
        entry["node"] = name;
        entry["ok"] = info.state_ok;
        entry["msg"] = info.state_msg;
        entry["ex"] = info.state_ex;
        entry["last_seen"] = std::chrono::duration_cast<std::chrono::seconds>(
            info.last_seen.time_since_epoch()).count();
        result.push_back(entry);
    }
    return result;
}

std::string Leader::generate_uuid7() const {
    // Simple UUID v7 generator (time-ordered)
    thread_local  std::random_device rd;
    thread_local  std::mt19937_64 gen(rd());
    thread_local  std::uniform_int_distribution<uint64_t> dis;

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    uint64_t rand_a = dis(gen);
    uint64_t rand_b = dis(gen);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(12) << (timestamp & 0xFFFFFFFFFFFF) << '-'
        << std::setw(4) << ((timestamp >> 48) & 0xFFFF) << '-'
        << '7' << std::setw(3) << ((rand_a >> 16) & 0xFFF) << '-'
        << std::setw(1) << ((rand_a >> 12) & 0x3) << std::setw(3) << (rand_a & 0xFFF) << '-'
        << std::setw(12) << rand_b;
    return oss.str();
}

} // namespace lunaricorn