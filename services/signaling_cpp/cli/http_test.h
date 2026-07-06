#pragma once

#include <atomic>
#include <thread>
#include <stop_token>
#include <string>
#include <cstdint>

/// Periodically tests the HTTP REST endpoints of the signaling service
/// (GET /health, GET /v1/stat, POST /v1/push, GET /v1/list/*, GET /v1/stat/clients)
/// and prints results with ✅/❌ indicators.
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
    uint64_t get_list_ok() const;
    uint64_t get_clients_ok() const;
    uint64_t get_root_ok() const;
    uint64_t get_error_count() const;

private:
    /// Background thread function.
    void runner(std::stop_token stopToken);

    /// Perform GET / and print result.
    void test_root();

    /// Perform GET /health and print result.
    void test_health();

    /// Perform GET /stat and print result.
    void test_stat();

    /// Perform POST /push with a small test payload and print result.
    void test_push();

    /// Perform GET /v1/list/tags and print result.
    void test_list_tags();

    /// Perform GET /v1/list/types and print result.
    void test_list_types();

    /// Perform GET /v1/list/affected and print result.
    void test_list_affected();

    /// Perform GET /v1/list/owners and print result.
    void test_list_owners();

    /// Perform GET /v1/stat/clients and print result.
    void test_clients();

    /// Perform POST /v1/browse and print result.
    void test_browse();

    /// Build base URL string.
    std::string base_url() const;

    /// Perform a generic GET request and return response info.
    std::pair<int, std::string> do_get(const std::string& path);

    std::atomic<bool> m_running{false};
    std::string m_host;
    uint16_t m_port{8081};

    std::thread m_thread;
    std::stop_source m_stop_source;

    std::atomic<uint64_t> m_health_ok{0};
    std::atomic<uint64_t> m_stat_ok{0};
    std::atomic<uint64_t> m_push_ok{0};
    std::atomic<uint64_t> m_list_ok{0};
    std::atomic<uint64_t> m_clients_ok{0};
    std::atomic<uint64_t> m_root_ok{0};
    std::atomic<uint64_t> m_error_count{0};
};
