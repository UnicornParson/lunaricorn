#include <iostream>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <boost/algorithm/string.hpp>
#include <Poco/DateTime.h>
#include <Poco/DateTimeFormatter.h>
#include <lunaricorn.h>
#include "storage.h"
#include "endpoint.h"

using namespace lunaricorn;

int main() {
    std::cout << "[" << current_time_str() << "] starting maintenance service" << std::endl;

    const DbConfig cfg = loadConfigFromEnvironment();
    if (!cfg.valid())
    {
        std::cerr << "Invalid database configuration" << std::endl;
        return -1;
    }

    std::shared_ptr<PGStorage> storage = std::make_shared<PGStorage>(cfg);
    std::shared_ptr<Pusher> pusher = std::make_shared<Pusher>(storage);


    ServerConfig serverCfg;
    serverCfg.num_threads = get_workers(4);
    LogCollectorServer endpoint(pusher, storage, serverCfg);
    pusher->start();
    endpoint.run();
    std::cout << "[" << current_time_str() << "] exit maintenance service" << std::endl;
    return 0;
}