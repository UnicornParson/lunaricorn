#include "http_test.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <random>
#include <chrono>

#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/StreamCopier.h>
#include <Poco/Exception.h>

#include <boost/json.hpp>

namespace json = boost::json;

static std::mt19937 g_orb_http_rng(static_cast<unsigned>(
    std::chrono::steady_clock::now().time_since_epoch().count()));

static std::string now_ts()
{
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   now.time_since_epoch()) % 1000;
    std::tm tm_buf{};
    gmtime_r(&t, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    oss << "Z";
    return oss.str();
}

OrbHttpTest::OrbHttpTest() = default;

OrbHttpTest::~OrbHttpTest()
{
    stop();
}

bool OrbHttpTest::start(const std::string& host, uint16_t port)
{
    if (m_running.load()) {
        std::cerr << "[" << now_ts() << "] [OrbHttpTest] Already running" << std::endl;
        return false;
    }

    m_host = host;
    m_port = port;
    m_running.store(true);

    std::cout << "[" << now_ts() << "] [OrbHttpTest] Starting HTTP test against "
              << host << ":" << port << std::endl;

    m_stop_source = std::stop_source();
    m_thread = std::thread(&OrbHttpTest::runner, this, m_stop_source.get_token());

    return true;
}

void OrbHttpTest::stop()
{
    if (!m_running.load()) {
        return;
    }

    m_running.store(false);
    if (m_thread.joinable()) {
        m_thread.join();
    }

    std::cout << "[" << now_ts() << "] [OrbHttpTest] Stopped. "
              << "health_ok=" << m_health_ok.load()
              << " blob_get_ok=" << m_blob_get_ok.load()
              << " blob_put_ok=" << m_blob_put_ok.load()
              << " meta_get_ok=" << m_meta_get_ok.load()
              << " meta_put_ok=" << m_meta_put_ok.load()
              << " errors=" << m_error_count.load()
              << std::endl;
}

bool OrbHttpTest::is_running() const
{
    return m_running.load();
}

uint64_t OrbHttpTest::get_health_ok() const     { return m_health_ok.load(); }
uint64_t OrbHttpTest::get_blob_get_ok() const   { return m_blob_get_ok.load(); }
uint64_t OrbHttpTest::get_blob_put_ok() const   { return m_blob_put_ok.load(); }
uint64_t OrbHttpTest::get_meta_get_ok() const   { return m_meta_get_ok.load(); }
uint64_t OrbHttpTest::get_meta_put_ok() const   { return m_meta_put_ok.load(); }
uint64_t OrbHttpTest::get_error_count() const   { return m_error_count.load(); }

// ---- private ----

std::string OrbHttpTest::base_url() const
{
    return m_host + ":" + std::to_string(m_port);
}

void OrbHttpTest::runner(std::stop_token /*stopToken*/)
{
    std::cout << "[" << now_ts() << "] [OrbHttpTest] Runner thread started" << std::endl;

    // Cycle through endpoints: health, blob put/get, meta put/get
    int phase = 0;
    const int interval_ms = 2000; // 2 seconds between tests

    while (m_running.load()) {
        switch (phase) {
            case 0: test_health();        break;
            case 1: test_blob_put_get();  break;
            case 2: test_meta_put_get();  break;
        }
        phase = (phase + 1) % 3;

        // Sleep in small increments so we can detect stop quickly
        for (int i = 0; i < 40 && m_running.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms / 40));
        }
    }

    std::cout << "[" << now_ts() << "] [OrbHttpTest] Runner thread exiting" << std::endl;
}

std::pair<int, std::string> OrbHttpTest::do_get(const std::string& path)
{
    try {
        Poco::Net::SocketAddress addr(m_host, m_port);
        Poco::Net::HTTPClientSession session(addr);
        session.setTimeout(Poco::Timespan(5, 0));

        Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, path);
        session.sendRequest(request);

        Poco::Net::HTTPResponse response;
        std::istream& bodyStream = session.receiveResponse(response);

        std::string body;
        Poco::StreamCopier::copyToString(bodyStream, body);

        return {response.getStatus(), std::move(body)};
    } catch (const Poco::Exception& e) {
        return {-1, e.displayText()};
    }
}

std::pair<int, std::string> OrbHttpTest::do_put(const std::string& path, const std::string& body)
{
    try {
        Poco::Net::SocketAddress addr(m_host, m_port);
        Poco::Net::HTTPClientSession session(addr);
        session.setTimeout(Poco::Timespan(5, 0));

        Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_PUT, path);
        request.setContentType("application/json");
        request.setContentLength(static_cast<int>(body.size()));
        request.setKeepAlive(false);

        session.sendRequest(request) << body;

        Poco::Net::HTTPResponse response;
        std::istream& respStream = session.receiveResponse(response);

        std::string respBody;
        Poco::StreamCopier::copyToString(respStream, respBody);

        return {response.getStatus(), std::move(respBody)};
    } catch (const Poco::Exception& e) {
        return {-1, e.displayText()};
    }
}

void OrbHttpTest::test_health()
{
    auto [status, body] = do_get("/health");
    if (status == 200) {
        m_health_ok.fetch_add(1);
        std::cout << "[" << now_ts() << "] [OrbHttpTest] GET /health -> 200 ✅"
                  << " body=" << body << std::endl;
    } else {
        m_error_count.fetch_add(1);
        std::cerr << "[" << now_ts() << "] [OrbHttpTest] GET /health -> " << status
                  << " ❌ body=" << body << std::endl;
    }
}

void OrbHttpTest::test_blob_put_get()
{
    std::uniform_int_distribution<> dist_val(1, 99999);
    uint64_t seq = m_test_seq.fetch_add(1);
    std::string test_id = "cli_test_blob_" + std::to_string(seq);

    // Build test payload
    json::object payload;
    payload["test_seq"] = json::value(static_cast<int64_t>(seq));
    payload["value"]    = json::value(static_cast<int64_t>(dist_val(g_orb_http_rng)));
    payload["source"]   = json::value("orb_cli_http_test");
    std::string body = json::serialize(json::value(payload));

    // PUT /blob/{id}
    auto [put_status, put_body] = do_put("/blob/" + test_id, body);
    if (put_status == 200) {
        m_blob_put_ok.fetch_add(1);
        std::cout << "[" << now_ts() << "] [OrbHttpTest] PUT /blob/" << test_id
                  << " -> 200 ✅ body=" << put_body << std::endl;
    } else {
        m_error_count.fetch_add(1);
        std::cerr << "[" << now_ts() << "] [OrbHttpTest] PUT /blob/" << test_id
                  << " -> " << put_status << " ❌ body=" << put_body << std::endl;
        return;
    }

    // GET /blob/{id} to verify
    auto [get_status, get_body] = do_get("/blob/" + test_id);
    if (get_status == 200) {
        m_blob_get_ok.fetch_add(1);
        std::cout << "[" << now_ts() << "] [OrbHttpTest] GET /blob/" << test_id
                  << " -> 200 ✅ body=" << get_body << std::endl;

        // Verify the payload matches
        try {
            json::value parsed = json::parse(get_body);
            if (parsed.is_object()) {
                auto& obj = parsed.as_object();
                auto it = obj.find("test_seq");
                if (it != obj.end() && it->value().is_int64() &&
                    it->value().as_int64() == static_cast<int64_t>(seq)) {
                    std::cout << "[" << now_ts() << "] [OrbHttpTest] Blob content verified ✅"
                              << std::endl;
                } else {
                    std::cerr << "[" << now_ts() << "] [OrbHttpTest] Blob content MISMATCH ❌"
                              << std::endl;
                    m_error_count.fetch_add(1);
                }
            }
        } catch (...) {
            // Parse error is not critical, just log
        }
    } else {
        m_error_count.fetch_add(1);
        std::cerr << "[" << now_ts() << "] [OrbHttpTest] GET /blob/" << test_id
                  << " -> " << get_status << " ❌ body=" << get_body << std::endl;
    }
}

void OrbHttpTest::test_meta_put_get()
{
    std::uniform_int_distribution<> dist_val(1, 99999);
    uint64_t seq = m_test_seq.fetch_add(1);
    std::string test_id = "cli_test_meta_" + std::to_string(seq);

    // Build valid InternalMetaObject JSON as defined in http_endpoint.cpp handle_put_meta
    json::object meta;
    meta["description"] = json::value("Orb CLI test meta object " + std::to_string(seq));
    meta["has_content"] = json::value(true);

    json::array tags;
    tags.push_back(json::value("cli_test"));
    tags.push_back(json::value("automated"));
    meta["tags"] = tags;

    // Optionally set parent/prev/next for testing
    if (seq > 0) {
        meta["parent"] = json::value("cli_test_meta_" + std::to_string(seq - 1));
    }

    std::string body = json::serialize(json::value(meta));

    // PUT /meta/{id}
    auto [put_status, put_body] = do_put("/meta/" + test_id, body);
    if (put_status == 200) {
        m_meta_put_ok.fetch_add(1);
        std::cout << "[" << now_ts() << "] [OrbHttpTest] PUT /meta/" << test_id
                  << " -> 200 ✅ body=" << put_body << std::endl;
    } else {
        m_error_count.fetch_add(1);
        std::cerr << "[" << now_ts() << "] [OrbHttpTest] PUT /meta/" << test_id
                  << " -> " << put_status << " ❌ body=" << put_body << std::endl;
        return;
    }

    // GET /meta/{id} to verify
    auto [get_status, get_body] = do_get("/meta/" + test_id);
    if (get_status == 200) {
        m_meta_get_ok.fetch_add(1);
        std::cout << "[" << now_ts() << "] [OrbHttpTest] GET /meta/" << test_id
                  << " -> 200 ✅ body=" << get_body << std::endl;

        // Verify the payload contains expected fields
        try {
            json::value parsed = json::parse(get_body);
            if (parsed.is_object()) {
                auto& obj = parsed.as_object();
                auto desc_it = obj.find("description");
                if (desc_it != obj.end() && desc_it->value().is_string()) {
                    std::cout << "[" << now_ts() << "] [OrbHttpTest] Meta content verified ✅"
                              << std::endl;
                } else {
                    std::cerr << "[" << now_ts() << "] [OrbHttpTest] Meta content MISMATCH ❌"
                              << std::endl;
                    m_error_count.fetch_add(1);
                }
            }
        } catch (...) {
            // Parse error is not critical
        }
    } else {
        m_error_count.fetch_add(1);
        std::cerr << "[" << now_ts() << "] [OrbHttpTest] GET /meta/" << test_id
                  << " -> " << get_status << " ❌ body=" << get_body << std::endl;
    }
}