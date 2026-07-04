#pragma once

#include <atomic>
#include <thread>
#include <stop_token>
#include <memory>
#include <string>
#include <chrono>

#include "signaling_api.h"

class TestSender
{
public:
    TestSender();
    ~TestSender();

    // Start the test sender thread
    // Returns true if thread was started, false if already running or connection failed
    bool start(const std::string& host, uint16_t port);

    // Signal the thread to stop
    void stop();

    // Check if the thread is still running
    bool is_running() const;

    // Get statistics
    uint64_t get_sent_count() const;
    uint64_t get_error_count() const;

private:
    // The thread function that runs in its own thread
    void runner(std::stop_token stopToken);

    // Create and send a test event + verify via query
    void send_and_verify();

    // Callback for responses from test messages
    void on_test_response(const lunaricorn::SignalingResponse& resp);

    std::atomic<bool> m_running{false};
    std::atomic<uint64_t> m_sent_count{0};
    std::atomic<uint64_t> m_error_count{0};

    std::string m_host;
    uint16_t m_port;

    std::thread m_thread;
    std::stop_source m_stop_source;
    std::shared_ptr<lunaricorn::SignalingConnector> m_connector;

    // Track last pushed event type for query verification
    std::string m_last_event_type;
    int m_last_event_type_idx{0};
};