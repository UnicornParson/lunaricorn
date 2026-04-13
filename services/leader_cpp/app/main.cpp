#include <iostream>
#include <lunaricorn.h>
int main() {
    std::cout << "OK" << std::endl;
}

/*
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <boost/algorithm/string.hpp>
#include <Poco/DateTime.h>
#include <Poco/DateTimeFormatter.h>
#include "storage.h"
#include "endpoint.h"

[[maybe_unused]] constexpr DbConfig defaultCfg 
{
    .dbType = "postgresql",
    .dbPort = "8003",
    .dbUser = "lunaricorn",
    .dbDbname = "lunaricorn"
};


DbConfig loadConfigFromEnvironment() {
    struct EnvMapping {
        std::string DbConfig::*field;
        std::string envVarName;
        std::optional<std::string> defaultValue;
    };

    // Маппинг аналогичный Python-версии
    const std::vector<EnvMapping> mappings = {
        {&DbConfig::dbType,      "db_type",     "postgresql"},
        {&DbConfig::dbHost,      "db_host",     std::nullopt},
        {&DbConfig::dbPort,      "db_port",     "5432"},
        {&DbConfig::dbUser,      "db_user",     "lunaricorn"},
        {&DbConfig::dbPassword,  "db_password", std::nullopt},
        {&DbConfig::dbDbname,    "db_name",     "lunaricorn"}
    };

    DbConfig config;
    std::vector<std::string> missingVars;

    for (const auto& m : mappings) {
        const char* envValue = std::getenv(m.envVarName.c_str());
        if (envValue == nullptr) {
            if (m.defaultValue.has_value()) {
                config.*(m.field) = *m.defaultValue;
            } else {
                missingVars.push_back(m.envVarName);
            }
        } else {
            config.*(m.field) = envValue;
        }
    }

    if (!missingVars.empty()) {
        std::string errorMsg = "Required environment variables are missing: ";
        for (size_t i = 0; i < missingVars.size(); ++i) {
            if (i > 0) errorMsg += ", ";
            errorMsg += missingVars[i];
        }
        errorMsg += "\nPlease add them to your Docker Compose or .env file:\n";
        for (const auto& var : missingVars) {
            errorMsg += "  - " + var + "\n";
        }
        errorMsg += "See project README or documentation for details.";
        throw std::runtime_error(errorMsg);
    }

    return config;
}

int get_workers(int default_val) {
    const char* env_value = std::getenv("WORKERS");
    if (env_value == nullptr) {
        return default_val;
    }

    try {
        std::string val_str(env_value);
        size_t start = val_str.find_first_not_of(" \t");
        if (start == std::string::npos) {
            return default_val;
        }
        size_t end = val_str.find_last_not_of(" \t");
        val_str = val_str.substr(start, end - start + 1);

        int workers = std::stoi(val_str);

        if (workers > 0) {
            return workers;
        }
    } catch (const std::invalid_argument&) {
        std::cerr << "Invalid WORKERS value: not a number" << std::endl;
    } catch (const std::out_of_range&) {
        std::cerr << "WORKERS value out of range" << std::endl;
    }

    return default_val;
}


int main() {
    std::cout << "[" << current_time_str() << "] starting maintenance service" << std::endl;

    const DbConfig cfg = loadConfigFromEnvironment();
    if (!cfg.valid())
    {
        std::cerr << "Invalid database configuration" << std::endl;
        return -1;
    }

    std::shared_ptr<PGStorage> storage = std::make_shared<PGStorage>(cfg);
    ServerConfig serverCfg;
    serverCfg.num_threads = get_workers(4);
    LogCollectorServer endpoint(storage, serverCfg);
    endpoint.run();
    std::cout << "[" << current_time_str() << "] exit maintenance service" << std::endl;
    return 0;
}
    */