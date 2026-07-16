#include "orb_client.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <sstream>
#include <iostream>
#include <regex>
#include <zlib.h>

#include <lunaricorn.h>

// -------------------------------------------------------------------
// OrbClient
// -------------------------------------------------------------------

OrbClient::OrbClient(asio::io_context& ioc, const std::string& server_url)
    : ioc_(ioc)
    , health_timer_(ioc, std::chrono::seconds(HEALTH_CHECK_INTERVAL_SEC))
{
    parse_url(server_url);
}

OrbClient::~OrbClient()
{
    stop_health_check();
}

void OrbClient::parse_url(const std::string& url)
{
    // Parse http://host:port or http://host:port/path
    std::regex url_re(R"(^http://([^:/]+)(?::(\d+))?(/.*)?$)");
    std::smatch m;
    if(std::regex_match(url, m, url_re)) {
        host_ = m[1].str();
        if(m[2].matched)
            port_ = static_cast<unsigned short>(std::stoi(m[2].str()));
        if(m[3].matched)
            target_prefix_ = m[3].str();
        // Remove trailing slash from prefix
        if(!target_prefix_.empty() && target_prefix_.back() == '/')
            target_prefix_.pop_back();
    } else {
        throw std::invalid_argument("Invalid server URL: " + url);
    }
}

// -------------------------------------------------------------------
// Decompress gzip
// -------------------------------------------------------------------

std::string OrbClient::decompress_gzip(const std::string& data)
{
    if(data.empty())
        return {};

    z_stream strm = {};
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;

    int ret = inflateInit2(&strm, 15 + 32);  // auto-detect gzip header
    if(ret != Z_OK)
        throw std::runtime_error("inflateInit2 failed");

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
            throw std::runtime_error("inflate failed");
        }

        decompressed.append(outbuf, sizeof(outbuf) - strm.avail_out);
    } while(ret != Z_STREAM_END);

    inflateEnd(&strm);
    return decompressed;
}

// -------------------------------------------------------------------
// do_request - synchronous HTTP request via Boost.Beast
// -------------------------------------------------------------------

OrbClient::HttpResponse OrbClient::do_request(
    const std::string& method,
    const std::string& path,
    const std::string& body)
{
    HttpResponse result;
    result.status = -1;

    try {
        // Resolve host
        tcp::resolver resolver(ioc_);
        auto const results = resolver.resolve(host_, std::to_string(port_));

        // Connect socket
        beast::tcp_stream stream(ioc_);
        stream.connect(results);

        // Build full path
        std::string full_path = target_prefix_ + path;
        if(full_path.empty())
            full_path = "/";

        // Build request
        http::request<http::string_body> req;
        req.version(11);
        req.method(method);
        req.target(full_path);
        req.set(http::field::host, host_);
        req.set(http::field::user_agent, "orb_client/1.0");
        req.set(http::field::accept, "application/json");
        req.set(http::field::accept_encoding, "gzip");

        if(!body.empty()) {
            req.body() = body;
            req.set(http::field::content_type, "application/json");
            req.prepare_payload();
        }

        // Send request
        http::write(stream, req);

        // Receive response
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        result.status = res.result_int();

        // Check if response is gzip-encoded
        auto ce_it = res.find(http::field::content_encoding);
        bool is_gzip = false;
        if(ce_it != res.end()) {
            is_gzip = (ce_it->value().find("gzip") != std::string::npos);
        }

        if(is_gzip) {
            result.body = decompress_gzip(res.body());
        } else {
            result.body = res.body();
        }

        // Graceful close
        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    } catch(const std::exception& e) {
        MLOG_E("OrbClient::do_request({} {}): exception: {}", method, path, e.what());
        result.status = -1;
        result.body = e.what();
    }

    return result;
}

// -------------------------------------------------------------------
// IOrbController implementation
// -------------------------------------------------------------------

bool OrbClient::health()
{
    auto resp = do_request("GET", "/health");
    bool ok = (resp.status == 200);
    if(!ok) {
        MLOG_W("OrbClient::health: failed status={}", resp.status);
    }
    return ok;
}

std::optional<OrbMetaData> OrbClient::get_meta(const std::string& id)
{
    auto resp = do_request("GET", "/meta/" + id);
    if(resp.status != 200)
        return std::nullopt;

    try {
        json::value parsed = json::parse(resp.body);
        if(!parsed.is_object())
            return std::nullopt;

        const auto& obj = parsed.as_object();
        OrbMetaData data;
        if(obj.contains("id") && obj.at("id").is_string())
            data.id = obj.at("id").as_string().c_str();
        if(obj.contains("parent") && obj.at("parent").is_string())
            data.parent = obj.at("parent").as_string().c_str();
        if(obj.contains("prev") && obj.at("prev").is_string())
            data.prev = obj.at("prev").as_string().c_str();
        if(obj.contains("next") && obj.at("next").is_string())
            data.next = obj.at("next").as_string().c_str();
        if(obj.contains("tags") && obj.at("tags").is_array()) {
            for(const auto& t : obj.at("tags").as_array()) {
                if(t.is_string())
                    data.tags.push_back(t.as_string().c_str());
            }
        }
        if(obj.contains("description") && obj.at("description").is_string())
            data.description = obj.at("description").as_string().c_str();
        if(obj.contains("has_content") && obj.at("has_content").is_bool())
            data.has_content = obj.at("has_content").as_bool();

        return data;
    } catch(const std::exception& e) {
        MLOG_E("OrbClient::get_meta({}): parse error: {}", id, e.what());
        return std::nullopt;
    }
}

bool OrbClient::put_meta(const std::string& id, const OrbMetaData& data)
{
    json::object obj;
    obj["id"] = json::value(data.id);
    if(data.parent) obj["parent"] = json::value(*data.parent);
    if(data.prev)   obj["prev"] = json::value(*data.prev);
    if(data.next)   obj["next"] = json::value(*data.next);
    json::array tags;
    for(const auto& t : data.tags)
        tags.push_back(json::value(t));
    obj["tags"] = tags;
    obj["description"] = json::value(data.description);
    // has_content is server-managed, don't send it

    std::string body = json::serialize(json::value(obj));
    auto resp = do_request("PUT", "/meta/" + id, body);
    return resp.status == 200;
}

std::optional<json::object> OrbClient::get_blob(const std::string& id)
{
    auto resp = do_request("GET", "/blob/" + id);
    if(resp.status != 200)
        return std::nullopt;

    try {
        json::value parsed = json::parse(resp.body);
        if(!parsed.is_object())
            return std::nullopt;
        return parsed.as_object();
    } catch(const std::exception& e) {
        MLOG_E("OrbClient::get_blob({}): parse error: {}", id, e.what());
        return std::nullopt;
    }
}

bool OrbClient::put_blob(const std::string& id, const json::object& data)
{
    std::string body = json::serialize(json::value(data));
    auto resp = do_request("PUT", "/blob/" + id, body);
    return resp.status == 200;
}

std::vector<OrbMetaData> OrbClient::search_by_tags(const std::vector<std::string>& tags)
{
    std::vector<OrbMetaData> results;

    // Build comma-separated tags
    std::string tags_str;
    for(size_t i = 0; i < tags.size(); ++i) {
        if(i > 0) tags_str += ",";
        tags_str += tags[i];
    }

    auto resp = do_request("GET", "/search?tags=" + tags_str);
    if(resp.status != 200)
        return results;

    try {
        json::value parsed = json::parse(resp.body);
        if(!parsed.is_array())
            return results;

        for(const auto& item : parsed.as_array()) {
            if(!item.is_object()) continue;
            const auto& obj = item.as_object();

            OrbMetaData data;
            if(obj.contains("id") && obj.at("id").is_string())
                data.id = obj.at("id").as_string().c_str();
            if(obj.contains("parent") && obj.at("parent").is_string())
                data.parent = obj.at("parent").as_string().c_str();
            if(obj.contains("prev") && obj.at("prev").is_string())
                data.prev = obj.at("prev").as_string().c_str();
            if(obj.contains("next") && obj.at("next").is_string())
                data.next = obj.at("next").as_string().c_str();
            if(obj.contains("tags") && obj.at("tags").is_array()) {
                for(const auto& t : obj.at("tags").as_array()) {
                    if(t.is_string())
                        data.tags.push_back(t.as_string().c_str());
                }
            }
            if(obj.contains("description") && obj.at("description").is_string())
                data.description = obj.at("description").as_string().c_str();
            if(obj.contains("has_content") && obj.at("has_content").is_bool())
                data.has_content = obj.at("has_content").as_bool();

            results.push_back(std::move(data));
        }
    } catch(const std::exception& e) {
        MLOG_E("OrbClient::search_by_tags: parse error: {}", e.what());
    }

    return results;
}

std::string OrbClient::generate_id()
{
    auto resp = do_request("GET", "/gen_id");
    if(resp.status != 200)
        return {};

    try {
        json::value parsed = json::parse(resp.body);
        if(parsed.is_object() && parsed.as_object().contains("id") &&
           parsed.as_object().at("id").is_string()) {
            return parsed.as_object().at("id").as_string().c_str();
        }
    } catch(const std::exception& e) {
        MLOG_E("OrbClient::generate_id: parse error: {}", e.what());
    }

    return {};
}

// -------------------------------------------------------------------
// Health check timer
// -------------------------------------------------------------------

void OrbClient::start_health_check()
{
    server_alive_ = false;
    on_health_timer({});
}

void OrbClient::stop_health_check()
{
    beast::error_code ec;
    health_timer_.cancel(ec);
}

void OrbClient::on_health_timer(beast::error_code ec)
{
    if(ec) return;

    bool alive = health();
    if(alive != server_alive_) {
        server_alive_ = alive;
        MLOG_I("OrbClient: server status changed to {}", alive ? "ALIVE" : "DEAD");
        if(status_cb_) {
            status_cb_(alive);
        }
    }

    health_timer_.expires_after(std::chrono::seconds(HEALTH_CHECK_INTERVAL_SEC));
    health_timer_.async_wait(
        [self = shared_from_this()](beast::error_code inner_ec) {
            self->on_health_timer(inner_ec);
        });
}