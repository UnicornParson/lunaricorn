#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/StreamCopier.h>
#include <Poco/Exception.h>

#include <boost/json.hpp>

#include "http_test.h"

static std::atomic<bool> g_running{true};
static std::atomic<OrbHttpTest*> g_http_test{nullptr};

static void signal_handler(int signum)
{
    (void)signum;
    g_running = false;
    OrbHttpTest* ht = g_http_test.load();
    if (ht) {
        ht->stop();
    }
}

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

/// Quick connectivity check: GET /health to verify server is reachable.
static bool check_server_alive(const std::string& host, uint16_t port)
{
    try {
        Poco::Net::SocketAddress addr(host, port);
        Poco::Net::HTTPClientSession session(addr);
        session.setTimeout(Poco::Timespan(5, 0));

        Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_GET, "/health");
        session.sendRequest(request);

        Poco::Net::HTTPResponse response;
        std::istream& bodyStream = session.receiveResponse(response);

        std::string body;
        Poco::StreamCopier::copyToString(bodyStream, body);

        return response.getStatus() == 200;
    } catch (const Poco::Exception& e) {
        std::cerr << "[" << now_ts() << "] Connection check failed: "
                  << e.displayText() << std::endl;
        return false;
    }
}

static void print_usage(const char* prog)
{
    std::cout << "Usage: " << prog << " [host] [port]" << std::endl;
    std::cout << std::endl;
    std::cout << "Orb CLI - integration test tool for the orb_cpp service." << std::endl;
    std::cout << std::endl;
    std::cout << "Environment variables:" << std::endl;
    std::cout << "  ORB_HOST  - orb service host (default: 127.0.0.1)" << std::endl;
    std::cout << "  ORB_PORT  - orb service port (default: 8081)" << std::endl;
    std::cout << std::endl;
    std::cout << "Tests the following HTTP endpoints:" << std::endl;
    std::cout << "  GET  /health          - service health check" << std::endl;
    std::cout << "  GET  /blob/{id}       - load blob data" << std::endl;
    std::cout << "  PUT  /blob/{id}       - store blob data" << std::endl;
    std::cout << "  GET  /meta/{id}       - load meta object" << std::endl;
    std::cout << "  PUT  /meta/{id}       - store meta object" << std::endl;
}

int main(int argc, char* argv[])
{
    // Install signal handlers
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Read config from environment or arguments
    std::string host = "127.0.0.1";
    uint16_t port  = 8081;

    const char* env_host = std::getenv("ORB_HOST");
    const char* env_port = std::getenv("ORB_PORT");
    if (env_host) host = env_host;
    if (env_port) port = static_cast<uint16_t>(std::stoi(env_port));

    if (argc > 1) host = argv[1];
    if (argc > 2) port = static_cast<uint16_t>(std::stoi(argv[2]));

    // Print help if requested
    if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        print_usage(argv[0]);
        return 0;
    }

    std::cout << "=== Orb CLI ===" << std::endl;
    std::cout << "Target: " << host << ":" << port << std::endl;
    std::cout << "Press Ctrl+C to exit" << std::endl;
    std::cout << "================" << std::endl;

    // Quick connectivity check
    std::cout << "[" << now_ts() << "] Checking server availability..." << std::endl;
    if (!check_server_alive(host, port)) {
        std::cerr << "[" << now_ts() << "] Server at " << host << ":" << port
                  << " is not responding to GET /health" << std::endl;
        std::cerr << "[" << now_ts() << "] Make sure the orb_cpp service is running." << std::endl;
        return 1;
    }
    std::cout << "[" << now_ts() << "] Server is alive ✅" << std::endl;
    std::cout << std::endl;

    // Start HTTP test thread
    OrbHttpTest http_test;
    g_http_test.store(&http_test);

    if (http_test.start(host, port)) {
        std::cout << "[" << now_ts() << "] HTTP test started against "
                  << host << ":" << port << std::endl;
    } else {
        std::cerr << "[" << now_ts() << "] Failed to start HTTP test" << std::endl;
        return 1;
    }

    std::cout << std::endl;

    // Wait for Ctrl+C
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << std::endl << "[" << now_ts() << "] Exiting..." << std::endl;
    return 0;
}