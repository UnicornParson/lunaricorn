#include "maintenance.h"
#include "stdafx.h"
#include <sstream>
#include <iostream>
#include <Poco/Net/HTTPMessage.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/JSON/Stringifier.h>

using namespace lunaricorn;

LogCollectorClient& LogCollectorClient::instance() {
    static std::once_flag init_flag;
    static std::unique_ptr<LogCollectorClient> instance_ptr;

    std::call_once(init_flag, []() {
        const char* host_env = std::getenv("MAINTENANCE_HOST");
        const char* port_env = std::getenv("MAINTENANCE_PORT");

        if (!host_env) {
            throw std::runtime_error("Environment variable MAINTENANCE_HOST is not set");
        }
        if (!port_env) {
            throw std::runtime_error("Environment variable MAINTENANCE_PORT is not set");
        }

        std::string host(host_env);
        int port = std::stoi(port_env);
        std::string base_url = "http://" + host + ":" + std::to_string(port);
        instance_ptr = std::unique_ptr<LogCollectorClient>(new LogCollectorClient(std::move(base_url)));
    });

    return *instance_ptr;
}

LogCollectorClient::LogCollectorClient(std::string base_url,
                                       int timeout_sec,
                                       int max_retries,
                                       std::chrono::milliseconds retry_delay)
    : base_url_(std::move(base_url))
    , timeout_(timeout_sec)
    , max_retries_(max_retries)
    , retry_delay_(retry_delay) {
    if (base_url_.back() == '/') {
        base_url_.pop_back();
    }
}

std::string LogCollectorClient::build_url(std::string_view path) const {
    return base_url_ + std::string(path);
}

Poco::JSON::Object::Ptr LogCollectorClient::handle_json_response(Poco::Net::HTTPResponse& response,
                                                                 std::istream& body_stream) {
    if (response.getStatus() != Poco::Net::HTTPResponse::HTTP_OK) {
        throw Poco::Net::HTTPException("HTTP error: " + std::to_string(response.getStatus()) +
                                       " " + response.getReason());
    }
    std::string content_type = response.get("Content-Type", "");
    if (content_type.find("application/json") == std::string::npos) {
        throw std::runtime_error("Response is not JSON");
    }
    Poco::JSON::Parser parser;
    Poco::Dynamic::Var result = parser.parse(body_stream);
    return result.extract<Poco::JSON::Object::Ptr>();
}

std::string LogCollectorClient::handle_text_response(Poco::Net::HTTPResponse& response,
                                                     std::istream& body_stream) {
    if (response.getStatus() != Poco::Net::HTTPResponse::HTTP_OK) {
        throw Poco::Net::HTTPException("HTTP error: " + std::to_string(response.getStatus()) +
                                       " " + response.getReason());
    }
    std::ostringstream oss;
    Poco::StreamCopier::copyStream(body_stream, oss);
    return oss.str();
}

std::string LogCollectorClient::extract_filename_from_content_disposition(const std::string& header) const {
    std::string filename = "logs.txt";
    size_t pos = header.find("filename=");
    if (pos != std::string::npos) {
        std::string after = header.substr(pos + 9); // 9 = length of "filename="
        // Trim quotes if present
        if (!after.empty() && (after.front() == '"' || after.front() == '\'')) {
            after.erase(0, 1);
        }
        if (!after.empty() && (after.back() == '"' || after.back() == '\'')) {
            after.pop_back();
        }
        filename = after;
    }
    return filename;
}

Poco::JSON::Object::Ptr LogCollectorClient::get_status() {
    Poco::URI uri(build_url("/"));
    Poco::Net::HTTPClientSession session(uri.getHost(), uri.getPort());
    session.setTimeout(Poco::Timespan(timeout_, 0));

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, uri.getPathAndQuery());
    request.set("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:122.0) Gecko/20100101 Firefox/122.0");
    request.set("Accept", "application/json");
    request.set("Accept-Encoding", "gzip, deflate, br");
    request.set("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    request.set("Pragma", "no-cache");
    request.set("Expires", "0");
    request.set("Connection", "close");

    session.sendRequest(request);
    Poco::Net::HTTPResponse response;
    std::istream& body = session.receiveResponse(response);
    return handle_json_response(response, body);
}

bool LogCollectorClient::health_check() {
    try {
        Poco::URI uri(build_url("/health"));
        Poco::Net::HTTPClientSession session(uri.getHost(), uri.getPort());
        session.setTimeout(Poco::Timespan(timeout_, 0));

        Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, uri.getPathAndQuery());
        request.set("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:122.0) Gecko/20100101 Firefox/122.0");
        request.set("Accept", "application/json");
        request.set("Accept-Encoding", "gzip, deflate, br");
        request.set("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        request.set("Pragma", "no-cache");
        request.set("Expires", "0");
        request.set("Connection", "close");

        session.sendRequest(request);
        Poco::Net::HTTPResponse response;
        session.receiveResponse(response);
        return response.getStatus() == Poco::Net::HTTPResponse::HTTP_OK;
    } catch (const Poco::Exception&) {
        return false;
    }
}

Poco::JSON::Object::Ptr LogCollectorClient::send_log(std::string_view owner,
                                                     std::string_view token,
                                                     std::string_view message,
                                                     std::string_view log_type,
                                                     const std::optional<std::string>& datetime) {
    return with_retry([&]() -> Poco::JSON::Object::Ptr {
        Poco::JSON::Object payload;
        payload.set("o", std::string(owner));
        payload.set("t", std::string(token));
        payload.set("m", std::string(message));
        payload.set("type", std::string(log_type));
        if (datetime) {
            payload.set("dt", *datetime);
        }

        std::ostringstream json_stream;
        Poco::JSON::Stringifier::stringify(payload, json_stream);
        std::string json_body = json_stream.str();

        Poco::URI uri(build_url("/log"));
        Poco::Net::HTTPClientSession session(uri.getHost(), uri.getPort());
        session.setTimeout(Poco::Timespan(2, 0)); // Fixed 2 second timeout for send

        Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_POST, uri.getPathAndQuery());
        request.setContentType("application/json");
        request.set("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:122.0) Gecko/20100101 Firefox/122.0");
        request.set("Accept", "application/json");
        request.set("Accept-Encoding", "gzip, deflate, br");
        request.set("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        request.set("Pragma", "no-cache");
        request.set("Expires", "0");
        request.set("Connection", "close");
        request.setContentLength(json_body.size());

        std::ostream& request_body = session.sendRequest(request);
        request_body << json_body;

        Poco::Net::HTTPResponse response;
        std::istream& response_body = session.receiveResponse(response);
        return handle_json_response(response, response_body);
    });
}

Poco::JSON::Object::Ptr LogCollectorClient::send_logs_batch(const std::vector<Poco::JSON::Object::Ptr>& logs) {
    return with_retry([&]() -> Poco::JSON::Object::Ptr {
        Poco::JSON::Array batch_array;
        for (const auto& log : logs) {
            Poco::JSON::Object item;
            item.set("o", log->getValue<std::string>("o"));
            item.set("t", log->getValue<std::string>("t"));
            item.set("m", log->getValue<std::string>("m"));
            item.set("type", log->getValue<std::string>("type"));
            if (log->has("datetime")) {
                item.set("dt", log->getValue<std::string>("datetime"));
            }
            batch_array.add(item);
        }

        std::ostringstream json_stream;
        Poco::JSON::Stringifier::stringify(batch_array, json_stream);
        std::string json_body = json_stream.str();

        Poco::URI uri(build_url("/log/batch"));
        Poco::Net::HTTPClientSession session(uri.getHost(), uri.getPort());
        session.setTimeout(Poco::Timespan(2, 0));

        Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_POST, uri.getPathAndQuery());
        request.setContentType("application/json");
        request.set("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:122.0) Gecko/20100101 Firefox/122.0");
        request.set("Accept", "application/json");
        request.set("Accept-Encoding", "gzip, deflate, br");
        request.set("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        request.set("Pragma", "no-cache");
        request.set("Expires", "0");
        request.set("Connection", "close");
        request.setContentLength(json_body.size());

        std::ostream& request_body = session.sendRequest(request);
        request_body << json_body;

        Poco::Net::HTTPResponse response;
        std::istream& response_body = session.receiveResponse(response);
        return handle_json_response(response, response_body);
    });
}

std::vector<Poco::JSON::Object::Ptr> LogCollectorClient::pull_logs(int offset) {
    Poco::URI uri(build_url("/log/pull"));
    uri.addQueryParameter("offset", std::to_string(offset));

    Poco::Net::HTTPClientSession session(uri.getHost(), uri.getPort());
    session.setTimeout(Poco::Timespan(timeout_, 0));

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, uri.getPathAndQuery());
    request.set("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:122.0) Gecko/20100101 Firefox/122.0");
    request.set("Accept", "application/json");
    request.set("Accept-Encoding", "gzip, deflate, br");
    request.set("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    request.set("Pragma", "no-cache");
    request.set("Expires", "0");
    request.set("Connection", "close");

    session.sendRequest(request);
    Poco::Net::HTTPResponse response;
    std::istream& body = session.receiveResponse(response);
    Poco::JSON::Object::Ptr json_response = handle_json_response(response, body);

    std::vector<Poco::JSON::Object::Ptr> result;
    if (json_response->has("logs")) {
        Poco::JSON::Array::Ptr logs_array = json_response->getArray("logs");
        for (std::size_t i = 0; i < logs_array->size(); ++i) {
            result.push_back(logs_array->getObject(i));
        }
    }
    return result;
}

std::string LogCollectorClient::pull_logs_plain(int offset) {
    Poco::URI uri(build_url("/log/pull-plain"));
    uri.addQueryParameter("offset", std::to_string(offset));

    Poco::Net::HTTPClientSession session(uri.getHost(), uri.getPort());
    session.setTimeout(Poco::Timespan(timeout_, 0));

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, uri.getPathAndQuery());
    request.set("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:122.0) Gecko/20100101 Firefox/122.0");
    request.set("Accept", "application/json");
    request.set("Accept-Encoding", "gzip, deflate, br");
    request.set("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    request.set("Pragma", "no-cache");
    request.set("Expires", "0");
    request.set("Connection", "close");

    session.sendRequest(request);
    Poco::Net::HTTPResponse response;
    std::istream& body = session.receiveResponse(response);
    return handle_text_response(response, body);
}

std::pair<std::string, std::string> LogCollectorClient::download_logs_plain(int offset) {
    Poco::URI uri(build_url("/log/download-plain"));
    uri.addQueryParameter("offset", std::to_string(offset));

    Poco::Net::HTTPClientSession session(uri.getHost(), uri.getPort());
    session.setTimeout(Poco::Timespan(timeout_, 0));

    Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, uri.getPathAndQuery());
    request.set("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:122.0) Gecko/20100101 Firefox/122.0");
    request.set("Accept", "application/json");
    request.set("Accept-Encoding", "gzip, deflate, br");
    request.set("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    request.set("Pragma", "no-cache");
    request.set("Expires", "0");
    request.set("Connection", "close");

    session.sendRequest(request);
    Poco::Net::HTTPResponse response;
    std::istream& body = session.receiveResponse(response);
    std::string content = handle_text_response(response, body);
    std::string filename = extract_filename_from_content_disposition(response.get("Content-Disposition", ""));
    return {std::move(content), std::move(filename)};
}

void LogCollectorClient::close() {
    // No persistent session, nothing to close explicitly.
}