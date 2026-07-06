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

static std::mt19937 g_http_rng(static_cast<unsigned>(
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

HttpTest::HttpTest() = default;

HttpTest::~HttpTest()
{
    stop();
}

bool HttpTest::start(const std::string& host, uint16_t port)
{
    if (m_running.load()) {
        std::cerr << "[" << now_ts() << "] [HttpTest] Already running" << std::endl;
        return false;
    }

    m_host = host;
    m_port = port;
    m_running.store(true);

    std::cout << "[" << now_ts() << "] [HttpTest] Starting HTTP test against "
              << host << ":" << port << std::endl;

    m_stop_source = std::stop_source();
    m_thread = std::thread(&HttpTest::runner, this, m_stop_source.get_token());

    return true;
}

void HttpTest::stop()
{
    if (!m_running.load()) {
        return;
    }

    m_running.store(false);
    if (m_thread.joinable()) {
        m_thread.join();
    }

    std::cout << "[" << now_ts() << "] [HttpTest] Stopped. "
              << "root_ok=" << m_root_ok.load()
              << " health_ok=" << m_health_ok.load()
              << " stat_ok=" << m_stat_ok.load()
              << " push_ok=" << m_push_ok.load()
              << " list_ok=" << m_list_ok.load()
              << " clients_ok=" << m_clients_ok.load()
              << " errors=" << m_error_count.load()
              << std::endl;
}

bool HttpTest::is_running() const
{
    return m_running.load();
}

uint64_t HttpTest::get_health_ok() const   { return m_health_ok.load(); }
uint64_t HttpTest::get_stat_ok() const     { return m_stat_ok.load(); }
uint64_t HttpTest::get_push_ok() const     { return m_push_ok.load(); }
uint64_t HttpTest::get_list_ok() const     { return m_list_ok.load(); }
uint64_t HttpTest::get_clients_ok() const  { return m_clients_ok.load(); }
uint64_t HttpTest::get_root_ok() const     { return m_root_ok.load(); }
uint64_t HttpTest::get_error_count() const { return m_error_count.load(); }

// ---- private ----

std::string HttpTest::base_url() const
{
    return m_host + ":" + std::to_string(m_port);
}

void HttpTest::runner(std::stop_token /*stopToken*/)
{
    std::cout << "[" << now_ts() << "] [HttpTest] Runner thread started" << std::endl;

    // Cycle through endpoints: root, health, list/tags, list/types, list/affected, list/owners, stat/clients, stat, push, browse
    int phase = 0;
    const int interval_ms = 2000; // 2 seconds between tests

    while (m_running.load()) {
        switch (phase) {
            case 0: test_root();      break;
            case 1: test_health();    break;
            case 2: test_list_tags(); break;
            case 3: test_list_types(); break;
            case 4: test_list_affected(); break;
            case 5: test_list_owners(); break;
            case 6: test_clients();   break;
            case 7: test_stat();      break;
            case 8: test_push();      break;
            case 9: test_browse();    break;
        }
        phase = (phase + 1) % 10;

        // Sleep in small increments so we can detect stop quickly
        for (int i = 0; i < 40 && m_running.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms / 40));
        }
    }

    std::cout << "[" << now_ts() << "] [HttpTest] Runner thread exiting" << std::endl;
}

void HttpTest::test_health()
{
    try {
        Poco::Net::SocketAddress addr(m_host, m_port);
        Poco::Net::HTTPClientSession session(addr);
        session.setTimeout(Poco::Timespan(5, 0)); // 5s timeout

        Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, "/health");
        session.sendRequest(request);

        Poco::Net::HTTPResponse response;
        std::istream& bodyStream = session.receiveResponse(response);

        std::string body;
        Poco::StreamCopier::copyToString(bodyStream, body);

        int status = response.getStatus();
        if (status == 200) {
            m_health_ok.fetch_add(1);
            std::cout << "[" << now_ts() << "] [HttpTest] GET /health -> 200 OK"
                      << " body=" << body << std::endl;
        } else {
            m_error_count.fetch_add(1);
            std::cerr << "[" << now_ts() << "] [HttpTest] GET /health -> " << status
                      << " body=" << body << std::endl;
        }
    } catch (const Poco::Exception& e) {
        m_error_count.fetch_add(1);
        std::cerr << "[" << now_ts() << "] [HttpTest] GET /health FAILED: "
                  << e.displayText() << std::endl;
    }
}

void HttpTest::test_stat()
{
    try {
        Poco::Net::SocketAddress addr(m_host, m_port);
        Poco::Net::HTTPClientSession session(addr);
        session.setTimeout(Poco::Timespan(5, 0));

        Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, "/v1/stat");
        session.sendRequest(request);

        Poco::Net::HTTPResponse response;
        std::istream& bodyStream = session.receiveResponse(response);

        std::string body;
        Poco::StreamCopier::copyToString(bodyStream, body);

        int status = response.getStatus();
        if (status == 200) {
            m_stat_ok.fetch_add(1);
            std::cout << "[" << now_ts() << "] [HttpTest] GET /v1/stat -> 200 OK"
                      << " body=" << body << std::endl;
        } else {
            m_error_count.fetch_add(1);
            std::cerr << "[" << now_ts() << "] [HttpTest] GET /v1/stat -> " << status
                      << " body=" << body << std::endl;
        }
    } catch (const Poco::Exception& e) {
        m_error_count.fetch_add(1);
        std::cerr << "[" << now_ts() << "] [HttpTest] GET /v1/stat FAILED: "
                  << e.displayText() << std::endl;
    }
}

void HttpTest::test_push()
{
    try {
        // Build a small test event payload
        std::uniform_int_distribution<> dist_val(1, 9999);
        boost::json::object payload;
        payload["type"]    = boost::json::value("test.http_push");
        payload["source"]  = boost::json::value("http_test");
        payload["value"]   = boost::json::value(static_cast<int64_t>(dist_val(g_http_rng)));

        boost::json::array affected;
        affected.push_back(boost::json::value("entity:system"));
        payload["affected"] = std::move(affected);

        std::string body = boost::json::serialize(payload);

        Poco::Net::SocketAddress addr(m_host, m_port);
        Poco::Net::HTTPClientSession session(addr);
        session.setTimeout(Poco::Timespan(5, 0));

        Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_POST, "/v1/push");
        request.setContentType("application/json");
        request.setContentLength(static_cast<int>(body.size()));
        request.setKeepAlive(false);

        session.sendRequest(request) << body;

        Poco::Net::HTTPResponse response;
        std::istream& respStream = session.receiveResponse(response);

        std::string respBody;
        Poco::StreamCopier::copyToString(respStream, respBody);

        int status = response.getStatus();
        if (status == 200) {
            m_push_ok.fetch_add(1);
            std::cout << "[" << now_ts() << "] [HttpTest] POST /v1/push -> 200 OK"
                      << " body=" << respBody << std::endl;
        } else {
            m_error_count.fetch_add(1);
            std::cerr << "[" << now_ts() << "] [HttpTest] POST /v1/push -> " << status
                      << " body=" << respBody << std::endl;
        }
    } catch (const Poco::Exception& e) {
        m_error_count.fetch_add(1);
        std::cerr << "[" << now_ts() << "] [HttpTest] POST /v1/push FAILED: "
                  << e.displayText() << std::endl;
    }
}

// ---- New compatibility API tests ----

std::pair<int, std::string> HttpTest::do_get(const std::string& path)
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
    } catch (const Poco::Exception&) {
        return {-1, ""};
    }
}

void HttpTest::test_root()
{
    auto [status, body] = do_get("/");
    if (status == 200) {
        m_root_ok.fetch_add(1);
        std::cout << "[" << now_ts() << "] [HttpTest] GET / -> 200 ✅ body=" << body << std::endl;
    } else {
        m_error_count.fetch_add(1);
        std::cerr << "[" << now_ts() << "] [HttpTest] GET / -> " << status << " ❌" << std::endl;
    }
}

void HttpTest::test_list_tags()
{
    auto [status, body] = do_get("/v1/list/tags");
    if (status == 200) {
        m_list_ok.fetch_add(1);
        std::cout << "[" << now_ts() << "] [HttpTest] GET /v1/list/tags -> 200 ✅ body=" << body << std::endl;
    } else {
        m_error_count.fetch_add(1);
        std::cerr << "[" << now_ts() << "] [HttpTest] GET /v1/list/tags -> " << status << " ❌" << std::endl;
    }
}

void HttpTest::test_list_types()
{
    auto [status, body] = do_get("/v1/list/types");
    if (status == 200) {
        m_list_ok.fetch_add(1);
        std::cout << "[" << now_ts() << "] [HttpTest] GET /v1/list/types -> 200 ✅ body=" << body << std::endl;
    } else {
        m_error_count.fetch_add(1);
        std::cerr << "[" << now_ts() << "] [HttpTest] GET /v1/list/types -> " << status << " ❌" << std::endl;
    }
}

void HttpTest::test_list_affected()
{
    auto [status, body] = do_get("/v1/list/affected");
    if (status == 200) {
        m_list_ok.fetch_add(1);
        std::cout << "[" << now_ts() << "] [HttpTest] GET /v1/list/affected -> 200 ✅ body=" << body << std::endl;
    } else {
        m_error_count.fetch_add(1);
        std::cerr << "[" << now_ts() << "] [HttpTest] GET /v1/list/affected -> " << status << " ❌" << std::endl;
    }
}

void HttpTest::test_list_owners()
{
    auto [status, body] = do_get("/v1/list/owners");
    if (status == 200) {
        m_list_ok.fetch_add(1);
        std::cout << "[" << now_ts() << "] [HttpTest] GET /v1/list/owners -> 200 ✅ body=" << body << std::endl;
    } else {
        m_error_count.fetch_add(1);
        std::cerr << "[" << now_ts() << "] [HttpTest] GET /v1/list/owners -> " << status << " ❌" << std::endl;
    }
}

void HttpTest::test_clients()
{
    auto [status, body] = do_get("/v1/stat/clients");
    if (status == 200) {
        m_clients_ok.fetch_add(1);
        std::cout << "[" << now_ts() << "] [HttpTest] GET /v1/stat/clients -> 200 ✅ body=" << body << std::endl;
    } else {
        m_error_count.fetch_add(1);
        std::cerr << "[" << now_ts() << "] [HttpTest] GET /v1/stat/clients -> " << status << " ❌" << std::endl;
    }
}

void HttpTest::test_browse()
{
    try {
        // First push a test event so we have something to browse
        std::uniform_int_distribution<> dist_val(1, 9999);
        int64_t testVal = dist_val(g_http_rng);

        boost::json::object pushPayload;
        pushPayload["type"]    = boost::json::value("test.browse");
        pushPayload["source"]  = boost::json::value("http_test");
        pushPayload["value"]   = boost::json::value(static_cast<int64_t>(testVal));

        boost::json::array affected;
        affected.push_back(boost::json::value("entity:system"));
        pushPayload["affected"] = std::move(affected);

        std::string pushBody = boost::json::serialize(pushPayload);

        Poco::Net::SocketAddress addr(m_host, m_port);
        Poco::Net::HTTPClientSession session(addr);
        session.setTimeout(Poco::Timespan(5, 0));

        Poco::Net::HTTPRequest pushRequest(Poco::Net::HTTPRequest::HTTP_POST, "/v1/push");
        pushRequest.setContentType("application/json");
        pushRequest.setContentLength(static_cast<int>(pushBody.size()));
        pushRequest.setKeepAlive(false);

        session.sendRequest(pushRequest) << pushBody;

        Poco::Net::HTTPResponse pushResponse;
        std::istream& pushRespStream = session.receiveResponse(pushResponse);
        std::string pushRespBody;
        Poco::StreamCopier::copyToString(pushRespStream, pushRespBody);

        // Now browse for events matching our test event
        boost::json::object browsePayload;
        browsePayload["event_types"] = boost::json::array{boost::json::value("test.browse")};
        browsePayload["sources"] = boost::json::array{boost::json::value("http_test")};
        browsePayload["limit"] = boost::json::value(static_cast<int64_t>(10));

        std::string browseBody = boost::json::serialize(browsePayload);

        Poco::Net::HTTPRequest browseRequest(Poco::Net::HTTPRequest::HTTP_POST, "/v1/browse");
        browseRequest.setContentType("application/json");
        browseRequest.setContentLength(static_cast<int>(browseBody.size()));
        browseRequest.setKeepAlive(false);

        session.sendRequest(browseRequest) << browseBody;

        Poco::Net::HTTPResponse browseResponse;
        std::istream& browseRespStream = session.receiveResponse(browseResponse);

        std::string browseRespBody;
        Poco::StreamCopier::copyToString(browseRespStream, browseRespBody);

        int browseStatus = browseResponse.getStatus();
        if (browseStatus == 200) {
            m_push_ok.fetch_add(1);
            std::cout << "[" << now_ts() << "] [HttpTest] POST /v1/browse -> 200 OK"
                      << " push_resp=" << pushRespBody
                      << " browse_body=" << browseRespBody << std::endl;
        } else {
            m_error_count.fetch_add(1);
            std::cerr << "[" << now_ts() << "] [HttpTest] POST /v1/browse -> " << browseStatus
                      << " body=" << browseRespBody << std::endl;
        }
    } catch (const Poco::Exception& e) {
        m_error_count.fetch_add(1);
        std::cerr << "[" << now_ts() << "] [HttpTest] POST /v1/browse FAILED: "
                  << e.displayText() << std::endl;
    }
}
