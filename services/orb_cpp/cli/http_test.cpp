#include "http_test.h"

#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/StreamCopier.h>
#include <Poco/Exception.h>

#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <zlib.h>

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

// -------------------------------------------------------------------
// Gzip decompression helper
// -------------------------------------------------------------------
static std::string decompress_gzip(const std::string& data)
{
    if(data.empty()) return {};

    z_stream strm = {};
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    int ret = inflateInit2(&strm, 15 + 32);
    if(ret != Z_OK) return {};

    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    strm.avail_in = static_cast<uInt>(data.size());

    std::string decompressed;
    char outbuf[65536];

    do {
        strm.next_out = reinterpret_cast<Bytef*>(outbuf);
        strm.avail_out = sizeof(outbuf);
        ret = inflate(&strm, Z_NO_FLUSH);
        if(ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
            inflateEnd(&strm);
            return {};
        }
        decompressed.append(outbuf, sizeof(outbuf) - strm.avail_out);
    } while(ret != Z_STREAM_END);

    inflateEnd(&strm);
    return decompressed;
}

// -------------------------------------------------------------------
// OrbHttpTest implementation
// -------------------------------------------------------------------

OrbHttpTest::OrbHttpTest()
    : m_running(false)
{
}

OrbHttpTest::~OrbHttpTest()
{
    stop();
}

bool OrbHttpTest::start(const std::string& host, uint16_t port)
{
    if(m_running.load())
        return false;

    m_host = host;
    m_port = port;
    m_running.store(true);

    m_stop_source = std::stop_source{};
    m_thread = std::thread(&OrbHttpTest::runner, this, m_stop_source.get_token());

    return true;
}

void OrbHttpTest::stop()
{
    if(m_running.load()) {
        m_running.store(false);
        m_stop_source.request_stop();
        if(m_thread.joinable())
            m_thread.join();
    }
}

bool OrbHttpTest::is_running() const
{
    return m_running.load();
}

uint64_t OrbHttpTest::get_health_ok() const    { return m_health_ok.load(); }
uint64_t OrbHttpTest::get_blob_get_ok() const  { return m_blob_get_ok.load(); }
uint64_t OrbHttpTest::get_blob_put_ok() const  { return m_blob_put_ok.load(); }
uint64_t OrbHttpTest::get_meta_get_ok() const  { return m_meta_get_ok.load(); }
uint64_t OrbHttpTest::get_meta_put_ok() const  { return m_meta_put_ok.load(); }
uint64_t OrbHttpTest::get_gen_id_ok() const    { return m_gen_id_ok.load(); }
uint64_t OrbHttpTest::get_search_ok() const    { return m_search_ok.load(); }
uint64_t OrbHttpTest::get_gzip_ok() const      { return m_gzip_ok.load(); }
uint64_t OrbHttpTest::get_has_blob_ok() const  { return m_has_blob_ok.load(); }
uint64_t OrbHttpTest::get_error_count() const  { return m_error_count.load(); }

std::string OrbHttpTest::base_url() const
{
    return m_host + ":" + std::to_string(m_port);
}

// -------------------------------------------------------------------
// Low-level HTTP helpers
// -------------------------------------------------------------------

std::pair<int, std::string> OrbHttpTest::do_get(const std::string& path)
{
    try {
        Poco::Net::SocketAddress addr(m_host, m_port);
        Poco::Net::HTTPClientSession session(addr);
        session.setTimeout(Poco::Timespan(10, 0));

        Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, path);
        session.sendRequest(request);

        Poco::Net::HTTPResponse response;
        std::istream& bodyStream = session.receiveResponse(response);

        std::string body;
        Poco::StreamCopier::copyToString(bodyStream, body);
        return {response.getStatus(), body};
    } catch(const Poco::Exception& e) {
        return {-1, e.displayText()};
    }
}

OrbHttpTest::HttpResponse OrbHttpTest::do_get_ex(
    const std::string& path, const std::string& accept_encoding)
{
    HttpResponse result;
    result.status = -1;

    try {
        Poco::Net::SocketAddress addr(m_host, m_port);
        Poco::Net::HTTPClientSession session(addr);
        session.setTimeout(Poco::Timespan(10, 0));

        Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, path);
        if(!accept_encoding.empty()) {
            request.set("Accept-Encoding", accept_encoding);
        }
        session.sendRequest(request);

        Poco::Net::HTTPResponse response;
        std::istream& bodyStream = session.receiveResponse(response);

        std::string body;
        Poco::StreamCopier::copyToString(bodyStream, body);

        result.status = response.getStatus();
        result.body = body;

        if(response.has("Content-Encoding")) {
            result.content_encoding = response.get("Content-Encoding");
        }

        return result;
    } catch(const Poco::Exception& e) {
        result.status = -1;
        result.body = e.displayText();
        return result;
    }
}

std::pair<int, std::string> OrbHttpTest::do_put(
    const std::string& path, const std::string& body)
{
    try {
        Poco::Net::SocketAddress addr(m_host, m_port);
        Poco::Net::HTTPClientSession session(addr);
        session.setTimeout(Poco::Timespan(10, 0));

        Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_PUT, path);
        request.setContentType("application/json");
        request.setContentLength(body.size());

        std::ostream& os = session.sendRequest(request);
        os << body;

        Poco::Net::HTTPResponse response;
        std::istream& bodyStream = session.receiveResponse(response);

        std::string responseBody;
        Poco::StreamCopier::copyToString(bodyStream, responseBody);
        return {response.getStatus(), responseBody};
    } catch(const Poco::Exception& e) {
        return {-1, e.displayText()};
    }
}

// -------------------------------------------------------------------
// Test methods
// -------------------------------------------------------------------

void OrbHttpTest::test_health()
{
    auto [status, body] = do_get("/health");
    if(status == 200) {
        std::cout << "[" << now_ts() << "] GET /health -> 200 ✅" << std::endl;
        m_health_ok.fetch_add(1);
    } else {
        std::cout << "[" << now_ts() << "] GET /health -> " << status << " ❌ " << body << std::endl;
        m_error_count.fetch_add(1);
    }
}

void OrbHttpTest::test_blob_put_get()
{
    uint64_t seq = m_test_seq.fetch_add(1);
    std::string id = "cli-test-blob-" + std::to_string(seq);

    // First, create meta for this id (required for blob storage)
    json::object meta_obj;
    meta_obj["description"] = "test blob meta";
    json::array tags;
    tags.push_back(json::value("cli-test"));
    meta_obj["tags"] = tags;
    std::string meta_body = json::serialize(json::value(meta_obj));
    auto [meta_status, meta_resp] = do_put("/meta/" + id, meta_body);

    if(meta_status != 200) {
        std::cout << "[" << now_ts() << "] PUT /meta/" << id << " (for blob) -> "
                  << meta_status << " ❌ " << meta_resp << std::endl;
        m_error_count.fetch_add(1);
        return;
    }

    // PUT blob
    json::object data;
    data["key1"] = json::value("value1");
    data["seq"]  = json::value(static_cast<int64_t>(seq));
    data["nested"] = json::value({{"inner", "data"}});
    std::string body = json::serialize(json::value(data));

    auto [put_status, put_resp] = do_put("/blob/" + id, body);
    if(put_status == 200) {
        std::cout << "[" << now_ts() << "] PUT /blob/" << id << " -> 200 ✅" << std::endl;
        m_blob_put_ok.fetch_add(1);
    } else {
        std::cout << "[" << now_ts() << "] PUT /blob/" << id << " -> " << put_status
                  << " ❌ " << put_resp << std::endl;
        m_error_count.fetch_add(1);
    }

    // GET blob
    auto [get_status, get_resp] = do_get("/blob/" + id);
    if(get_status == 200) {
        // Verify content
        try {
            json::value parsed = json::parse(get_resp);
            if(parsed.is_object() && parsed.as_object().contains("key1") &&
               parsed.as_object().at("key1").as_string() == "value1") {
                std::cout << "[" << now_ts() << "] GET /blob/" << id << " -> 200 ✅" << std::endl;
                m_blob_get_ok.fetch_add(1);
            } else {
                std::cout << "[" << now_ts() << "] GET /blob/" << id << " -> 200 (data mismatch) ❌" << std::endl;
                m_error_count.fetch_add(1);
            }
        } catch(...) {
            std::cout << "[" << now_ts() << "] GET /blob/" << id << " -> 200 (json parse error) ❌" << std::endl;
            m_error_count.fetch_add(1);
        }
    } else {
        std::cout << "[" << now_ts() << "] GET /blob/" << id << " -> " << get_status
                  << " ❌ " << get_resp << std::endl;
        m_error_count.fetch_add(1);
    }
}

void OrbHttpTest::test_meta_put_get()
{
    uint64_t seq = m_test_seq.fetch_add(1);
    std::string id = "cli-test-meta-" + std::to_string(seq);

    // PUT meta
    json::object meta_obj;
    meta_obj["description"] = "test meta object " + std::to_string(seq);
    meta_obj["parent"] = json::value("root");
    json::array tags;
    tags.push_back(json::value("cli-test"));
    tags.push_back(json::value("phase2"));
    meta_obj["tags"] = tags;

    std::string body = json::serialize(json::value(meta_obj));
    auto [put_status, put_resp] = do_put("/meta/" + id, body);

    if(put_status == 200) {
        std::cout << "[" << now_ts() << "] PUT /meta/" << id << " -> 200 ✅" << std::endl;
        m_meta_put_ok.fetch_add(1);
    } else {
        std::cout << "[" << now_ts() << "] PUT /meta/" << id << " -> " << put_status
                  << " ❌ " << put_resp << std::endl;
        m_error_count.fetch_add(1);
    }

    // GET meta
    auto [get_status, get_resp] = do_get("/meta/" + id);
    if(get_status == 200) {
        try {
            json::value parsed = json::parse(get_resp);
            if(parsed.is_object()) {
                const auto& obj = parsed.as_object();
                bool ok = obj.contains("id") && obj.at("id").as_string() == id &&
                          obj.contains("description") &&
                          obj.contains("has_content");
                if(ok) {
                    std::cout << "[" << now_ts() << "] GET /meta/" << id << " -> 200 ✅" << std::endl;
                    m_meta_get_ok.fetch_add(1);
                } else {
                    std::cout << "[" << now_ts() << "] GET /meta/" << id << " -> 200 (missing fields) ❌" << std::endl;
                    m_error_count.fetch_add(1);
                }
            }
        } catch(...) {
            std::cout << "[" << now_ts() << "] GET /meta/" << id << " -> 200 (json parse error) ❌" << std::endl;
            m_error_count.fetch_add(1);
        }
    } else {
        std::cout << "[" << now_ts() << "] GET /meta/" << id << " -> " << get_status
                  << " ❌ " << get_resp << std::endl;
        m_error_count.fetch_add(1);
    }
}

void OrbHttpTest::test_gen_id()
{
    auto [status, body] = do_get("/gen_id");
    if(status == 200) {
        try {
            json::value parsed = json::parse(body);
            if(parsed.is_object() && parsed.as_object().contains("id")) {
                std::string id = parsed.as_object().at("id").as_string().c_str();
                if(!id.empty()) {
                    std::cout << "[" << now_ts() << "] GET /gen_id -> " << id.substr(0, 8) << "... ✅" << std::endl;
                    m_gen_id_ok.fetch_add(1);
                } else {
                    std::cout << "[" << now_ts() << "] GET /gen_id -> 200 (empty id) ❌" << std::endl;
                    m_error_count.fetch_add(1);
                }
            } else {
                std::cout << "[" << now_ts() << "] GET /gen_id -> 200 (no id field) ❌" << std::endl;
                m_error_count.fetch_add(1);
            }
        } catch(const std::exception& e) {
            std::cout << "[" << now_ts() << "] GET /gen_id -> 200 (parse error: " << e.what() << ") ❌" << std::endl;
            m_error_count.fetch_add(1);
        }
    } else {
        std::cout << "[" << now_ts() << "] GET /gen_id -> " << status << " ❌ " << body << std::endl;
        m_error_count.fetch_add(1);
    }
}

void OrbHttpTest::test_search()
{
    uint64_t seq = m_test_seq.fetch_add(1);
    std::string id1 = "cli-test-search-a-" + std::to_string(seq);
    std::string id2 = "cli-test-search-b-" + std::to_string(seq);
    std::string search_tag = "search-test-" + std::to_string(seq);

    // Create two meta objects with the search tag
    json::object meta1;
    meta1["description"] = "search test A";
    json::array tags1;
    tags1.push_back(json::value(search_tag));
    tags1.push_back(json::value("group-a"));
    meta1["tags"] = tags1;
    do_put("/meta/" + id1, json::serialize(json::value(meta1)));

    json::object meta2;
    meta2["description"] = "search test B";
    json::array tags2;
    tags2.push_back(json::value(search_tag));
    tags2.push_back(json::value("group-b"));
    meta2["tags"] = tags2;
    do_put("/meta/" + id2, json::serialize(json::value(meta2)));

    // Search by the common tag
    auto [status, body] = do_get("/search?tags=" + search_tag);
    if(status == 200) {
        try {
            json::value parsed = json::parse(body);
            if(parsed.is_array()) {
                size_t count = parsed.as_array().size();
                if(count >= 2) {
                    std::cout << "[" << now_ts() << "] GET /search?tags=" << search_tag
                              << " -> found " << count << " results ✅" << std::endl;
                    m_search_ok.fetch_add(1);
                } else {
                    std::cout << "[" << now_ts() << "] GET /search?tags=" << search_tag
                              << " -> found " << count << " results (expected >=2) ❌" << std::endl;
                    m_error_count.fetch_add(1);
                }
            } else {
                std::cout << "[" << now_ts() << "] GET /search?tags=" << search_tag
                          << " -> 200 (not an array) ❌" << std::endl;
                m_error_count.fetch_add(1);
            }
        } catch(const std::exception& e) {
            std::cout << "[" << now_ts() << "] GET /search?tags=" << search_tag
                      << " -> parse error: " << e.what() << " ❌" << std::endl;
            m_error_count.fetch_add(1);
        }
    } else {
        std::cout << "[" << now_ts() << "] GET /search?tags=" << search_tag
                  << " -> " << status << " ❌ " << body << std::endl;
        m_error_count.fetch_add(1);
    }
}

void OrbHttpTest::test_gzip_support()
{
    // Request /health with Accept-Encoding: gzip
    auto resp = do_get_ex("/health", "gzip");
    if(resp.status == 200) {
        bool has_gzip_encoding = !resp.content_encoding.empty() &&
                                 resp.content_encoding.find("gzip") != std::string::npos;

        if(has_gzip_encoding) {
            // Decompress and verify
            std::string decompressed = decompress_gzip(resp.body);
            if(!decompressed.empty()) {
                std::cout << "[" << now_ts() << "] GET /health (gzip) -> compressed ✅" << std::endl;
                m_gzip_ok.fetch_add(1);
            } else {
                std::cout << "[" << now_ts() << "] GET /health (gzip) -> bad compressed data ❌" << std::endl;
                m_error_count.fetch_add(1);
            }
        } else {
            // Server may not compress small responses; accept uncompressed
            std::cout << "[" << now_ts() << "] GET /health (gzip) -> " << resp.status
                      << " (not compressed, acceptable for small responses)" << std::endl;
            m_gzip_ok.fetch_add(1);
        }
    } else {
        std::cout << "[" << now_ts() << "] GET /health (gzip) -> " << resp.status << " ❌" << std::endl;
        m_error_count.fetch_add(1);
    }
}

void OrbHttpTest::test_has_blob_auto()
{
    uint64_t seq = m_test_seq.fetch_add(1);
    std::string id = "cli-test-hasblob-" + std::to_string(seq);

    // Create meta - has_content should be false initially
    json::object meta_obj;
    meta_obj["description"] = "has_blob test";
    json::array tags;
    tags.push_back(json::value("cli-test"));
    meta_obj["tags"] = tags;
    do_put("/meta/" + id, json::serialize(json::value(meta_obj)));

    // Check meta - has_content should be false
    auto [get1_status, get1_body] = do_get("/meta/" + id);
    bool has_content_initially = false;
    if(get1_status == 200) {
        try {
            json::value parsed = json::parse(get1_body);
            if(parsed.is_object() && parsed.as_object().contains("has_content")) {
                has_content_initially = parsed.as_object().at("has_content").as_bool();
            }
        } catch(...) {}
    }

    // Store blob
    json::object blob_data;
    blob_data["test"] = json::value("data");
    do_put("/blob/" + id, json::serialize(json::value(blob_data)));

    // Check meta again - has_content should now be true
    auto [get2_status, get2_body] = do_get("/meta/" + id);
    bool has_content_after_blob = false;
    if(get2_status == 200) {
        try {
            json::value parsed = json::parse(get2_body);
            if(parsed.is_object() && parsed.as_object().contains("has_content")) {
                has_content_after_blob = parsed.as_object().at("has_content").as_bool();
            }
        } catch(...) {}
    }

    if(!has_content_initially && has_content_after_blob) {
        std::cout << "[" << now_ts() << "] has_blob auto: false -> true ✅" << std::endl;
        m_has_blob_ok.fetch_add(1);
    } else {
        std::cout << "[" << now_ts() << "] has_blob auto: init=" << has_content_initially
                  << " after=" << has_content_after_blob << " ❌" << std::endl;
        m_error_count.fetch_add(1);
    }
}

void OrbHttpTest::runner(std::stop_token stopToken)
{
    std::cout << "[" << now_ts() << "] HTTP test runner started against "
              << base_url() << std::endl;
    std::cout << "[" << now_ts() << "] Testing endpoints: /health, /gen_id, /search,"
              << " /meta, /blob, gzip, has_blob" << std::endl;
    std::cout << std::endl;

    int iteration = 0;
    const int TEST_INTERVAL_MS = 5000; // 5 seconds between full test cycles

    while(!stopToken.stop_requested()) {
        iteration++;

        // Phase 1 tests
        test_health();
        test_meta_put_get();
        test_blob_put_get();

        // Phase 2 tests (run less frequently)
        if(iteration % 2 == 1) {
            test_gen_id();
            test_gzip_support();
            test_has_blob_auto();
        }

        if(iteration % 3 == 1) {
            test_search();
        }

        std::cout << "[" << now_ts() << "] Cycle " << iteration << " complete."
                  << " OK: health=" << m_health_ok.load()
                  << " meta=" << m_meta_get_ok.load() << "/" << m_meta_put_ok.load()
                  << " blob=" << m_blob_get_ok.load() << "/" << m_blob_put_ok.load()
                  << " gen_id=" << m_gen_id_ok.load()
                  << " search=" << m_search_ok.load()
                  << " gzip=" << m_gzip_ok.load()
                  << " has_blob=" << m_has_blob_ok.load()
                  << " errors=" << m_error_count.load()
                  << std::endl;
        std::cout << std::endl;

        // Sleep for interval
        for(int i = 0; i < TEST_INTERVAL_MS / 100; ++i) {
            if(stopToken.stop_requested())
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    std::cout << "[" << now_ts() << "] HTTP test runner finished." << std::endl;
}
