#include <iostream>
#include <lunaricorn.h>
#include <config.h>
#include "stdafx.h"

#include "signaling_engine.h"
#include "signaling_engine_test.h"
#include "signal_waiter.h"
#include "raw_endpoint.h"

constexpr std::string app_name { "signaling" };
constexpr std::string app_ver { "0.2" };
constexpr std::string raw_host { "127.0.0.1" };
constexpr Poco::UInt16 raw_port = 8080;


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
    auto endpoint = std::make_shared<RawEndpoint>(raw_host, raw_port, engine);
    MLOG_D("create objects - ok");
    endpoint->start();
    signals.wait();
    endpoint->stop();

    MLOG_D("NORMAL EXIT {} {}, selftest_ok:{}", app_name, app_token, selftest_ok);
    return 0;
}
