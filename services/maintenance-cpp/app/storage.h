#pragma once

#include <string>
#include <vector>
#include <optional>
#include <iostream>
#include <memory>
#include <Poco/Data/Session.h>
#include <Poco/Data/SessionPool.h>
#include <Poco/Data/PostgreSQL/Connector.h>
#include <Poco/Data/Statement.h>
#include <Poco/Data/RecordSet.h>
#include <Poco/DateTime.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/Exception.h>

// -------------------- Configuration structure --------------------
struct DbConfig {
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
};

// -------------------- Record structure (result) --------------------
struct MaintenanceLogRecord {
    Poco::Int64 offset;
    std::string owner;
    std::string token;
    Poco::DateTime timestamputc;
    std::string msg;
};

// -------------------- Storage class --------------------
class PGStorage {
public:
    explicit PGStorage(const DbConfig& config);
    void testConnection();
    bool install();
    std::optional<Poco::Int64> push(const std::string& owner, const std::string& token, const std::string& msg);
    std::vector<MaintenanceLogRecord> pull(Poco::Int64 offset = 0, std::optional<int> limit = std::nullopt);
    std::vector<MaintenanceLogRecord> getAll();
    std::optional<MaintenanceLogRecord> getByOffset(Poco::Int64 offset);
    std::size_t countRecords();
    bool deleteByOffset(Poco::Int64 offset);

private:
    std::unique_ptr<Poco::Data::SessionPool> pool_;

    // Helper to check if the table already exists (by attempting to query it)
    bool tableExists();
};