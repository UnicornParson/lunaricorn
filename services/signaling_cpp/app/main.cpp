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

    // Check TEST_MODE environment variable for testing
    const char* test_mode_env = std::getenv("TEST_MODE");
    bool test_mode = (test_mode_env != nullptr && test_mode_env[0] != '\0');

    std::string leader_url;
    bool leader_enabled = true;

    if (test_mode) {
        // Test mode: skip leader connection entirely
        MLOG_W("TEST_MODE is set, leader connection disabled");
        leader_enabled = false;
    } else {
        // Normal mode: CLUSTER_LEADER_URL is required
        leader_url = std::getenv("CLUSTER_LEADER_URL");
        if (leader_url.empty()) {
            MLOG_E("CLUSTER_LEADER_URL is required in normal mode (set TEST_MODE=1 to disable leader)");
            return -1;
        }
        MLOG_D("Leader URL: {}", leader_url);
    }

    // Create LeaderConnector and wait for leader readiness
    std::unique_ptr<LeaderConnector> leader;
    bool leader_ready = false;

    if (leader_enabled) {
        leader = ConnectorUtils::create_leader_connector(leader_url);

        // Wait for leader with periodic logging (every 60s) - interruptible by signals
        MLOG_D("Waiting for leader at {}...", leader_url);
        int wait_count = 0;
        while (!leader_ready) {
            try {
                // Use short timeout wait so we can check for shutdown signals
                leader_ready = leader->wait_for_ready(5, 1);
                if (leader_ready) {
                    MLOG_D("Leader is ready");
                    break;
                }
            } catch (const std::exception& e) {
                MLOG_W("Failed to connect to leader: {}", e.what());
                // Continue retrying
            }

            wait_count++;
            // Log message every 60 seconds (assuming 1s check interval)
            if (wait_count % 60 == 0) {
                MLOG_W("Still waiting for leader at {}... ({}s elapsed)", leader_url, wait_count);
            }

            // Short sleep to avoid busy-waiting (1s interval between checks)
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (leader_ready) {
            MLOG_D("Leader connection established");
        }
    }

    // Register signaling service with the leader if available
    std::shared_ptr<RawEndpoint> endpoint;
    if (leader_enabled && leader_ready) {
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
    } else if (leader_enabled && !leader_ready) {
        MLOG_E("Leader not ready after extended wait, service running without cluster registration");
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
    if (leader_enabled && leader_ready && leader) {
        try {
            leader->stop_registration_timer();
            MLOG_D("Leader registration timer stopped");
        } catch (...) {
            // ignore on shutdown
        }
    }

    return 0;
}
