#pragma once

#include <atomic>
#include <thread>
#include <stop_token>
#include <string>
#include <cstdint>

/// Periodically tests the HTTP REST endpoints of the signaling service
/// (GET /health, GET /v1/stat, POST /v1/push) and prints results.
class HttpTest
{
public:
    HttpTest();
    ~HttpTest();

    /// Start the HTTP test timer in a background thread.
    /// Returns true if thread was started, false if already running.
    bool start(const std::string& host, uint16_t port);

    /// Signal the thread to stop.
    void stop();

    /// Check if the thread is still running.
    bool is_running() const;

    /// Get statistics.
    uint64_t get_health_ok() const;
    uint64_t get_stat_ok() const;
    uint64_t get_push_ok() const;
    uint64_t get_error_count() const;

private:
    /// Background thread function.
    void runner(std::stop_token stopToken);

    /// Perform GET /health and print result.
    void test_health();

    /// Perform GET /stat and print result.
    void test_stat();

    /// Perform POST /push with a small test payload and print result.
    void test_push();

    /// Build base URL string.
    std::string base_url() const;

    std::atomic<bool> m_running{false};
    std::string m_host;
    uint16_t m_port{8081};

    std::thread m_thread;
    std::stop_source m_stop_source;

    std::atomic<uint64_t> m_health_ok{0};
    std::atomic<uint64_t> m_stat_ok{0};
    std::atomic<uint64_t> m_push_ok{0};
    std::atomic<uint64_t> m_error_count{0};
};