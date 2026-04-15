#pragma once
#include "stdafx.h"
namespace lunaricorn
{
struct DbConfig
{
    std::string dbType;
    std::string dbHost;
    std::string dbPort;
    std::string dbUser;
    std::string dbPassword;
    std::string dbDbname;

    inline bool valid() const {
        return !dbType.empty() && !dbHost.empty() && !dbPort.empty() &&
               !dbUser.empty() && !dbPassword.empty() && !dbDbname.empty();
    }

    inline std::string toStr() const 
    {
        return dbUser + "@" + dbHost + ":" + dbPort + "/" + dbDbname;
    }
}; // struct DbConfig

[[maybe_unused]] constexpr DbConfig defaultCfg 
{
    .dbType = "postgresql",
    .dbPort = "8003",
    .dbUser = "lunaricorn",
    .dbDbname = "lunaricorn"
};


DbConfig loadConfigFromEnvironment();

int get_workers(int default_val);

} // namespace lunaricorn