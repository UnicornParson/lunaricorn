#include <iostream>
#include <lunaricorn.h>
#include "stdafx.h"
#include "leader.h"
#include "endpoint.h"


constexpr std::string app_name { "leader" };
constexpr std::string app_ver { "0.2" };
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
    MLOG_D("run {} {}", app_name, app_token);
    try
    {
        std::shared_ptr<Leader> leader = std::make_shared<Leader>();
        net::io_context ioc;
        auto endpoint = std::make_shared<Endpoint>(ioc, leader);
        endpoint->run(tcp::endpoint(net::ip::make_address("0.0.0.0"), 8000));
        ioc.run();
    } catch (const std::exception& e) {
        MLOG_E("Fatal error: {}", e.what());
        return 1;
    }
    MLOG_D("NORMAL EXIT {} {}", app_name, app_token);
}
