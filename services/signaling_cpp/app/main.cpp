#include <iostream>
#include <lunaricorn.h>
#include <config.h>
#include "stdafx.h"

#include "signaling_engine.h"
#include "signaling_engine_test.h"
#include "signal_waiter.h"
#include "raw_endpoint.h"
#include "http_endpoint.h"
#include "telemetry.h"
#include "leader_api.h"

constexpr std::string app_name { "signaling" };
constexpr std::string app_ver { "0.2" };
constexpr std::string raw_host { "127.0.0.1" };
constexpr Poco::UInt16 raw_port = 8080;
constexpr std::string http_host { "0.0.0.0" };
constexpr Poco::UInt16 http_port = 8081;


using namespace lunaricorn;

std::string get_instance_identifier()
{
    auto now = std::chrono::system_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  now.time_since_epoch())
                  .count();
    pid_t pid = getpid();
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<int> dist(100000, 999999);
    int random6 = dist(gen);
    return std::format("#{}_{}_{}_{:06d}", app_ver, ns, pid, random6);
}

int main() {
    const std::string app_token = get_instance_identifier();
    MLog::owner = app_name;
    MLog::token = app_token;
    MLog::is_stub = true;
    bool selftest_ok = false;
    MLOG_D("run {} {}", app_name, app_token);
    SignalWaiter signals;
    DbConfig dbcfg = loadConfigFromEnvironment();
    auto engine = make_engine(dbcfg);
    auto engine_test = std::make_shared<SignalingEngineTest>(engine);
    selftest_ok = engine_test->run();
    if (!selftest_ok)
    {
        MLOG_E("engine selftest failed");
        return -1;
    }

    // LeaderConnector integration: discover leader URL from environment
    std::string leader_url = std::getenv("CLUSTER_LEADER_URL");
    if (leader_url.empty()) {
        leader_url = "http://localhost:8001";
    }
    MLOG_D("Leader URL: {}", leader_url);

    // Create LeaderConnector and wait for leader readiness
    auto leader = ConnectorUtils::create_leader_connector(leader_url);

    // Optional: wait for leader to be ready
    bool leader_ready = false;
    try {
        leader_ready = leader->wait_for_ready(30, 3);
        if (leader_ready) {
            MLOG_D("Leader is ready");
        } else {
            MLOG_W("Leader did not become ready within timeout, continuing without registration");
        }
    } catch (const std::exception& e) {
        MLOG_W("Failed to connect to leader: {}, continuing without registration", e.what());
    }

    // Register signaling service with the leader if available
    std::shared_ptr<RawEndpoint> endpoint;
    if (leader_ready) {
        try {
            // Build additional info with internal service details
            Poco::JSON::Object::Ptr additional = new Poco::JSON::Object();
            additional->set("raw_port", raw_port);
            additional->set("http_port", http_port);
            additional->set("api_endpoint", std::format("http://{}:{}", http_host, http_port));

            // Register with the leader - this starts automatic periodic ping (imalive)
            auto reg_response = leader->register_service(
                app_name,           // node_name
                "signaling",        // node_type
                app_token,          // instance_key
                http_host,          // host
                http_port,          // port
                additional          // additional info
            );

            MLOG_D("Registered with leader: status={}",
                   reg_response->optValue<std::string>("status", "unknown"));
        } catch (const std::exception& e) {
            MLOG_W("Failed to register with leader: {}", e.what());
        }
    } else {
        MLOG_W("Skipping leader registration - leader not available");
    }

    endpoint = std::make_shared<RawEndpoint>(raw_host, raw_port, engine);

    // Connect engine to endpoint for subscriber event delivery
    endpoint->connectEngine(engine);

    // Create and start HTTP endpoint
    HttpServerConfig httpCfg;
    httpCfg.address = http_host;
    httpCfg.port = http_port;
    httpCfg.num_threads = 1;
    auto httpEndpoint = std::make_shared<HttpServer>(httpCfg);
    httpEndpoint->set_engine(engine);

    MLOG_D("create objects - ok");

    // Start periodic telemetry reporting (every 60 s via internal Poco::Timer)
    Telemetry::instance().start();

    httpEndpoint->start();
    endpoint->start();
    signals.wait();
    endpoint->stop();
    httpEndpoint->stop();

    Telemetry::instance().stop();

    MLOG_D("NORMAL EXIT {} {}, selftest_ok:{}", app_name, app_token, selftest_ok);

    // Stop periodic registration on shutdown
    if (leader_ready && leader) {
        try {
            leader->stop_registration_timer();
        } catch (...) {
            // ignore on shutdown
        }
    }

    return 0;
}
