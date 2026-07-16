#include "http_endpoint.h"

#include <boost/beast/http/parser.hpp>

#include <regex>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <zlib.h>

// -------------------------------------------------------------------
// HttpEndpoint
// -------------------------------------------------------------------

HttpEndpoint::HttpEndpoint(asio::io_context& ioc, const std::string& host, unsigned short port, Engine& engine)
    : ioc_(ioc)
    , acceptor_(ioc)
    , engine_(engine)
    , stats_timer_(ioc, std::chrono::seconds(60))
{
    beast::error_code ec;

    tcp::endpoint endpoint(asio::ip::make_address(host), port);

    acceptor_.open(endpoint.protocol(), ec);
    if(ec) throw std::runtime_error("acceptor open: " + ec.message());

    acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
    if(ec) throw std::runtime_error("acceptor set_option: " + ec.message());

    acceptor_.bind(endpoint, ec);
    if(ec) throw std::runtime_error("acceptor bind: " + ec.message());

    acceptor_.listen(asio::socket_base::max_listen_connections, ec);
    if(ec) throw std::runtime_error("acceptor listen: " + ec.message());
}

void HttpEndpoint::start()
{
    running_ = true;
    do_accept();
    start_stats_timer();
}

void HttpEndpoint::stop()
{
    running_ = false;
    beast::error_code ec;
    acceptor_.close(ec);
    stats_timer_.cancel();
}

void HttpEndpoint::do_accept()
{
    if(!running_) return;

    acceptor_.async_accept(
        ioc_,
        [self = shared_from_this()](beast::error_code ec, tcp::socket socket) {
            if(!ec) {
                std::make_shared<Session>(std::move(socket), *self)->run();
            }
            self->do_accept();
        });
}

// -------------------------------------------------------------------
// Gzip helpers
// -------------------------------------------------------------------

bool HttpEndpoint::client_accepts_gzip(const http::request<http::string_body>& req)
{
    auto it = req.find(http::field::accept_encoding);
    if(it == req.end())
        return false;
    const std::string& val = it->value();
    return val.find("gzip") != std::string::npos;
}

std::string HttpEndpoint::compress_gzip(const std::string& data)
{
    z_stream strm = {};
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    int ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                           15 + 16, 8, Z_DEFAULT_STRATEGY);
    if(ret != Z_OK)
        throw std::runtime_error("deflateInit2 failed");

    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    strm.avail_in = static_cast<uInt>(data.size());

    std::string compressed;
    compressed.resize(deflateBound(&strm, data.size()));

    strm.next_out = reinterpret_cast<Bytef*>(&compressed[0]);
    strm.avail_out = static_cast<uInt>(compressed.size());

    ret = deflate(&strm, Z_FINISH);
    if(ret != Z_STREAM_END) {
        deflateEnd(&strm);
        throw std::runtime_error("deflate failed");
    }

    compressed.resize(strm.total_out);
    deflateEnd(&strm);
    return compressed;
}

void HttpEndpoint::apply_gzip_if_needed(
    const http::request<http::string_body>& req,
    http::response<http::string_body>& res)
{
    if(!client_accepts_gzip(req))
        return;
    if(res.body().empty())
        return;

    std::string compressed = compress_gzip(res.body());
    res.body() = compressed;
    res.set(http::field::content_encoding, "gzip");
}

// -------------------------------------------------------------------
// Stats timer
// -------------------------------------------------------------------

void HttpEndpoint::start_stats_timer()
{
    if(!running_) return;

    stats_timer_.expires_after(std::chrono::seconds(60));
    stats_timer_.async_wait(
        [self = shared_from_this()](beast::error_code ec) {
            if(!ec)
                self->on_stats_timer(ec);
        });
}

void HttpEndpoint::on_stats_timer(beast::error_code /*ec*/)
{
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);

    std::ostringstream oss;
    oss << "[" << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ") << "] "
        << "[STATS] health=" << stats_.health_count.load()
        << " get_meta=" << stats_.get_meta_count.load()
        << " put_meta=" << stats_.put_meta_count.load()
        << " get_blob=" << stats_.get_blob_count.load()
        << " put_blob=" << stats_.put_blob_count.load()
        << " gen_id=" << stats_.gen_id_count.load()
        << " search=" << stats_.search_count.load()
        << " | bytes_in=" << stats_.bytes_in.load()
        << " bytes_out=" << stats_.bytes_out.load();

    MLOG_D("{}", oss.str());
    std::cout << oss.str() << std::endl;

    stats_.reset();
    start_stats_timer();
}

// -------------------------------------------------------------------
// Request routing
// -------------------------------------------------------------------

void HttpEndpoint::handle_request(
    http::request<http::string_body>& req,
    http::response<http::string_body>& res)
{
    res.set(http::field::server, "orb_cpp");
    res.set(http::field::content_type, "application/json");
    res.keep_alive(req.keep_alive());

    const std::string method = req.method_string();
    const std::string target = req.target();

    // Track bytes in
    stats_.bytes_in.fetch_add(req.body().size());

    try {
        // GET /health
        if(method == "GET" && target == "/health") {
            handle_health(res);
            return;
        }

        // GET /gen_id
        if(method == "GET" && target == "/gen_id") {
            handle_gen_id(res);
            return;
        }

        // GET /search?tags=...
        if(method == "GET" && target.find("/search") == 0) {
            std::string query;
            auto pos = target.find('?');
            if(pos != std::string::npos)
                query = target.substr(pos + 1);
            handle_search(query, res);
            return;
        }

        // GET /blob/{id}
        {
            static const std::regex blob_get_re(R"(^/blob/(.+)$)");
            std::smatch m;
            if(method == "GET" && std::regex_match(target, m, blob_get_re)) {
                handle_get_blob(m[1].str(), res);
                return;
            }
        }

        // PUT /blob/{id}
        {
            static const std::regex blob_put_re(R"(^/blob/(.+)$)");
            std::smatch m;
            if(method == "PUT" && std::regex_match(target, m, blob_put_re)) {
                handle_put_blob(m[1].str(), req.body(), res);
                return;
            }
        }

        // GET /meta/{id}
        {
            static const std::regex meta_get_re(R"(^/meta/(.+)$)");
            std::smatch m;
            if(method == "GET" && std::regex_match(target, m, meta_get_re)) {
                handle_get_meta(m[1].str(), res);
                return;
            }
        }

        // PUT /meta/{id}
        {
            static const std::regex meta_put_re(R"(^/meta/(.+)$)");
            std::smatch m;
            if(method == "PUT" && std::regex_match(target, m, meta_put_re)) {
                handle_put_meta(m[1].str(), req.body(), res);
                return;
            }
        }

        // Not found
        res.result(http::status::not_found);
        res.body() = R"({"error":"not found"})";
    } catch(const std::exception& e) {
        MLOG_E("handle_request exception: {}", e.what());
        res.result(http::status::internal_server_error);
        res.body() = R"({"error":")" + std::string(e.what()) + R"("})";
    }

    // Track bytes out and apply gzip
    stats_.bytes_out.fetch_add(res.body().size());
    apply_gzip_if_needed(req, res);
    res.prepare_payload();
}

void HttpEndpoint::handle_get_blob(
    const std::string& id,
    http::response<http::string_body>& res)
{
    auto data = engine_.load_blob(id);
    if(!data) {
        MLOG_D("GET /blob/{}: not found", id);
        res.result(http::status::not_found);
        res.body() = R"({"error":"blob not found"})";
        return;
    }

    MLOG_D("GET /blob/{}: found", id);
    res.result(http::status::ok);
    res.body() = json::serialize(json::value(*data));
    stats_.get_blob_count.fetch_add(1);
}

void HttpEndpoint::handle_put_blob(
    const std::string& id,
    const std::string& body,
    http::response<http::string_body>& res)
{
    json::value parsed = json::parse(body);
    if(!parsed.is_object()) {
        MLOG_E("PUT /blob/{}: invalid json, expected object", id);
        res.result(http::status::bad_request);
        res.body() = R"({"error":"invalid json, expected object"})";
        return;
    }

    if(engine_.store_blob(id, parsed.as_object())) {
        MLOG_D("PUT /blob/{}: stored", id);
        res.result(http::status::ok);
        res.body() = R"({"status":"stored"})";
        stats_.put_blob_count.fetch_add(1);
    } else {
        MLOG_E("PUT /blob/{}: store failed", id);
        res.result(http::status::internal_server_error);
        res.body() = R"({"error":"store failed"})";
    }
}

void HttpEndpoint::handle_get_meta(
    const std::string& id,
    http::response<http::string_body>& res)
{
    auto data = engine_.load_meta(id);
    if(!data) {
        MLOG_D("GET /meta/{}: not found", id);
        res.result(http::status::not_found);
        res.body() = R"({"error":"meta not found"})";
        return;
    }

    // Serialize InternalMetaObject to JSON
    json::object obj;
    obj["id"] = data->id;
    if(data->parent) obj["parent"] = *data->parent;
    if(data->prev)   obj["prev"] = *data->prev;
    if(data->next)   obj["next"] = *data->next;
    json::array tags;
    for(const auto& t : data->tags)
        tags.push_back(boost::json::value(t));
    obj["tags"] = tags;
    obj["description"] = data->description;
    obj["has_content"] = data->has_content;

    MLOG_D("GET /meta/{}: found desc='{}'", id, data->description);
    res.result(http::status::ok);
    res.body() = json::serialize(json::value(obj));
    stats_.get_meta_count.fetch_add(1);
}

void HttpEndpoint::handle_put_meta(
    const std::string& id,
    const std::string& body,
    http::response<http::string_body>& res)
{
    json::value parsed = json::parse(body);
    if(!parsed.is_object()) {
        MLOG_E("PUT /meta/{}: invalid json, expected object", id);
        res.result(http::status::bad_request);
        res.body() = R"({"error":"invalid json, expected object"})";
        return;
    }

    const json::object& obj = parsed.as_object();

    InternalMetaObject meta;
    meta.id = id;

    if(obj.contains("parent") && obj.at("parent").is_string())
        meta.parent = obj.at("parent").as_string().c_str();
    if(obj.contains("prev") && obj.at("prev").is_string())
        meta.prev = obj.at("prev").as_string().c_str();
    if(obj.contains("next") && obj.at("next").is_string())
        meta.next = obj.at("next").as_string().c_str();
    if(obj.contains("tags") && obj.at("tags").is_array()) {
        for(const auto& t : obj.at("tags").as_array()) {
            if(t.is_string())
                meta.tags.push_back(t.as_string().c_str());
        }
    }
    if(obj.contains("description") && obj.at("description").is_string())
        meta.description = obj.at("description").as_string().c_str();
    // has_content is managed automatically by Engine, ignore client value

    MLOG_D("PUT /meta/{}: desc='{}', tags_count={}, parent='{}'",
           id, meta.description, meta.tags.size(),
           meta.parent.value_or(""));

    if(engine_.store_meta(id, meta)) {
        MLOG_D("PUT /meta/{}: stored", id);
        res.result(http::status::ok);
        res.body() = R"({"status":"stored"})";
        stats_.put_meta_count.fetch_add(1);
    } else {
        MLOG_E("PUT /meta/{}: store failed", id);
        res.result(http::status::internal_server_error);
        res.body() = R"({"error":"store failed"})";
    }
}

void HttpEndpoint::handle_health(http::response<http::string_body>& res)
{
    if(engine_.ok()) {
        res.result(http::status::ok);
        res.body() = R"({"status":"ok"})";
    } else {
        MLOG_E("GET /health: engine not ok");
        res.result(http::status::service_unavailable);
        res.body() = R"({"status":"unavailable"})";
    }
    stats_.health_count.fetch_add(1);
}

void HttpEndpoint::handle_gen_id(http::response<http::string_body>& res)
{
    std::string id = engine_.generate_id();
    MLOG_D("GET /gen_id: generated id='{}'", id);
    res.result(http::status::ok);
    res.body() = R"({"id":")" + id + R"("})";
    stats_.gen_id_count.fetch_add(1);
}

void HttpEndpoint::handle_search(const std::string& query_string, http::response<http::string_body>& res)
{
    // Parse tags from query string: tags=tag1,tag2
    std::vector<std::string> tags;

    // Simple query string parser
    std::string tag_param;
    auto pos = query_string.find("tags=");
    if(pos != std::string::npos) {
        tag_param = query_string.substr(pos + 5);
        // Remove any trailing &...
        auto amp_pos = tag_param.find('&');
        if(amp_pos != std::string::npos)
            tag_param = tag_param.substr(0, amp_pos);
    }

    if(tag_param.empty()) {
        res.result(http::status::bad_request);
        res.body() = R"({"error":"missing tags parameter"})";
        return;
    }

    // Split by comma
    std::stringstream ss(tag_param);
    std::string tag;
    while(std::getline(ss, tag, ',')) {
        if(!tag.empty())
            tags.push_back(tag);
    }

    auto results = engine_.search_by_tags(tags);

    json::array arr;
    for(const auto& obj : results) {
        json::object item;
        item["id"] = obj.id;
        if(obj.parent) item["parent"] = *obj.parent;
        if(obj.prev)   item["prev"] = *obj.prev;
        if(obj.next)   item["next"] = *obj.next;
        json::array tgs;
        for(const auto& t : obj.tags)
            tgs.push_back(json::value(t));
        item["tags"] = tgs;
        item["description"] = obj.description;
        item["has_content"] = obj.has_content;
        arr.push_back(item);
    }

    MLOG_D("GET /search?tags={}: found {} results", tag_param, results.size());
    res.result(http::status::ok);
    res.body() = json::serialize(json::value(arr));
    stats_.search_count.fetch_add(1);
}

// -------------------------------------------------------------------
// Session
// -------------------------------------------------------------------

HttpEndpoint::Session::Session(tcp::socket&& socket, HttpEndpoint& owner)
    : stream_(std::move(socket))
    , owner_(owner)
{
}

void HttpEndpoint::Session::run()
{
    do_read();
}

void HttpEndpoint::Session::do_read()
{
    req_ = {};
    buffer_.consume(buffer_.size());

    http::async_read(
        stream_,
        buffer_,
        req_,
        [self = shared_from_this()](beast::error_code ec, size_t bytes_transferred) {
            self->on_read(ec, bytes_transferred);
        });
}

void HttpEndpoint::Session::on_read(beast::error_code ec, size_t /*bytes_transferred*/)
{
    if(ec == http::error::end_of_stream) {
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
        return;
    }

    if(ec) return;

    owner_.handle_request(req_, res_);
    do_write();
}

void HttpEndpoint::Session::do_write()
{
    http::async_write(
        stream_,
        res_,
        [self = shared_from_this()](beast::error_code ec, size_t bytes_transferred) {
            self->stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
        });
}
