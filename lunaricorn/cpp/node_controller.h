// node_controller.h
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include "maintenance.h"
class LeaderConnector;

class NodeController {
public:
    static NodeController& instance();
    static void abort_registration();

    bool register_node();

    NodeController(const NodeController&) = delete;
    NodeController& operator=(const NodeController&) = delete;

private:
    NodeController();
    ~NodeController() = default;

    std::string leader_url_;
    std::unique_ptr<LeaderConnector> connector_;
    bool leader_available_ = false;

    std::mutex abort_mutex_;
    std::condition_variable abort_cv_;
    std::atomic<bool> abort_flag_{false};

    const std::string node_key_ = "signaling_main";
    const std::string node_type_ = "signaling";
    const std::string node_name_ = "signaling";

    static std::unique_ptr<NodeController> instance_;
    static std::once_flag once_flag_;
};