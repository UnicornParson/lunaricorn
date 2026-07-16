#pragma once

#include <atomic>
#include <thread>
#include <stop_token>
#include <string>
#include <cstdint>
#include <boost/json.hpp>

/// Periodically tests the HTTP REST endpoints of the orb service
/// (GET /health, GET /blob/{id}, PUT /blob/{id}, GET /meta/{id}, PUT /meta/{id},
///  GET /gen_id, GET /search?tags=...)
/// and prints results with ✅/❌ indicators.
class OrbHttpTest
{
public:
    OrbHttpTest();
    ~OrbHttpTest();

    /// Start the HTTP test timer in a background thread.
    /// Returns true if thread was started, false if already running.
    bool start(const std::string& host, uint16_t port);

    /// Signal the thread to stop.
    void stop();

    /// Check if the thread is still running.
    bool is_running() const;

    /// Get statistics.
    uint64_t get_health_ok() const;
    uint64_t get_blob_get_ok() const;
    uint64_t get_blob_put_ok() const;
    uint64_t get_meta_get_ok() const;
    uint64_t get_meta_put_ok() const;
    uint64_t get_gen_id_ok() const;
    uint64_t get_search_ok() const;
    uint64_t get_gzip_ok() const;
    uint64_t get_has_blob_ok() const;
    uint64_t get_error_count() const;

private:
    /// Background thread function.
    void runner(std::stop_token stopToken);

    /// Perform GET /health and print result.
    void test_health();

    /// Perform PUT /blob/{id} with a test payload, then GET /blob/{id} to verify.
    void test_blob_put_get();

    /// Perform PUT /meta/{id} with a test payload, then GET /meta/{id} to verify.
    void test_meta_put_get();

    /// GET /gen_id, parse the returned id, verify it's non-empty.
    void test_gen_id();

    /// Create two meta objects with matching tags, then search by those tags.
    void test_search();

    /// Verify gzip-encoded response from the server (send Accept-Encoding: gzip).
    void test_gzip_support();

    /// Verify that has_blob is automatically set when blob is stored.
    void test_has_blob_auto();

    /// Build base URL string.
    std::string base_url() const;

    /// Perform a generic GET request and return response info.
    std::pair<int, std::string> do_get(const std::string& path);

    /// Perform a generic GET request with custom headers and return response info.
    struct HttpResponse {
        int status = 0;
        std::string body;
        std::string content_encoding;
    };
    HttpResponse do_get_ex(const std::string& path, const std::string& accept_encoding = "");

    /// Perform a generic PUT request with JSON body and return response info.
    std::pair<int, std::string> do_put(const std::string& path, const std::string& body);

    std::atomic<bool> m_running{false};
    std::string m_host;
    uint16_t m_port{8090};

    std::thread m_thread;
    std::stop_source m_stop_source;

    std::atomic<uint64_t> m_health_ok{0};
    std::atomic<uint64_t> m_blob_get_ok{0};
    std::atomic<uint64_t> m_blob_put_ok{0};
    std::atomic<uint64_t> m_meta_get_ok{0};
    std::atomic<uint64_t> m_meta_put_ok{0};
    std::atomic<uint64_t> m_gen_id_ok{0};
    std::atomic<uint64_t> m_search_ok{0};
    std::atomic<uint64_t> m_gzip_ok{0};
    std::atomic<uint64_t> m_has_blob_ok{0};
    std::atomic<uint64_t> m_error_count{0};

    /// Counter used to generate unique test IDs.
    std::atomic<uint64_t> m_test_seq{0};
};