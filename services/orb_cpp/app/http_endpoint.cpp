#include "http_endpoint.h"

#include <boost/beast/http/parser.hpp>

#include <regex>
#include <sstream>

// -------------------------------------------------------------------
// HttpEndpoint
// -------------------------------------------------------------------

HttpEndpoint::HttpEndpoint(asio::io_context& ioc, const std::string& host, unsigned short port, Engine& engine)
    : ioc_(ioc)
    , acceptor_(ioc)
    , engine_(engine)
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
}

void HttpEndpoint::stop()
{
    running_ = false;
    beast::error_code ec;
    acceptor_.close(ec);
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

void HttpEndpoint::handle_request(
    http::request<http::string_body>& req,
    http::response<http::string_body>& res)
{
    res.set(http::field::server, "orb_cpp");
    res.set(http::field::content_type, "application/json");
    res.keep_alive(req.keep_alive());

    const std::string method = req.method_string();
    const std::string target = req.target();

    try {
        // GET /health
        if(method == "GET" && target == "/health") {
            handle_health(res);
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
        res.result(http::status::internal_server_error);
        res.body() = R"({"error":")" + std::string(e.what()) + R"("})";
    }

    res.prepare_payload();
}

void HttpEndpoint::handle_get_blob(
    const std::string& id,
    http::response<http::string_body>& res)
{
    auto data = engine_.load_blob(id);
    if(!data) {
        res.result(http::status::not_found);
        res.body() = R"({"error":"blob not found"})";
        return;
    }

    res.result(http::status::ok);
    res.body() = json::serialize(json::value(*data));
}

void HttpEndpoint::handle_put_blob(
    const std::string& id,
    const std::string& body,
    http::response<http::string_body>& res)
{
    json::value parsed = json::parse(body);
    if(!parsed.is_object()) {
        res.result(http::status::bad_request);
        res.body() = R"({"error":"invalid json, expected object"})";
        return;
    }

    if(engine_.store_blob(id, parsed.as_object())) {
        res.result(http::status::ok);
        res.body() = R"({"status":"stored"})";
    } else {
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
        tags.push_back(t);
    obj["tags"] = tags;
    obj["description"] = data->description;
    obj["has_content"] = data->has_content;

    res.result(http::status::ok);
    res.body() = json::serialize(json::value(obj));
}

void HttpEndpoint::handle_put_meta(
    const std::string& id,
    const std::string& body,
    http::response<http::string_body>& res)
{
    json::value parsed = json::parse(body);
    if(!parsed.is_object()) {
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
    if(obj.contains("has_content") && obj.at("has_content").is_bool())
        meta.has_content = obj.at("has_content").as_bool();

    if(engine_.store_meta(id, meta)) {
        res.result(http::status::ok);
        res.body() = R"({"status":"stored"})";
    } else {
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
        res.result(http::status::service_unavailable);
        res.body() = R"({"status":"unavailable"})";
    }
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