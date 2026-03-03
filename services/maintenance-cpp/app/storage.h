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

using Poco::Data::Session;
using Poco::Data::Statement;
using Poco::Data::SessionPool;
using Poco::DateTime;
using Poco::Exception;
using Poco::DateTimeFormatter;

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
    DateTime timestamputc;
    std::string msg;
};

// -------------------- Storage class --------------------
class PGStorage {
public:
    explicit PGStorage(const DbConfig& config);

    // Test the connection by executing a simple query
    void testConnection();

    // Create the table and indexes if they do not exist
    bool install();

    // Insert a new record. Returns the generated offset, or std::nullopt on failure.
    std::optional<Poco::Int64> push(const std::string& owner, const std::string& token, const std::string& msg);

    // Retrieve records with offset > given value, ordered by offset.
    std::vector<MaintenanceLogRecord> pull(Poco::Int64 offset = 0, std::optional<int> limit = std::nullopt);

    // Get all records (offset > -1 effectively means all)
    std::vector<MaintenanceLogRecord> getAll();

    // Get a single record by its offset.
    std::optional<MaintenanceLogRecord> getByOffset(Poco::Int64 offset);

    // Count total records in the table.
    std::size_t countRecords();

    // Delete a record by offset. Returns true if deleted, false if not found or error.
    bool deleteByOffset(Poco::Int64 offset);

private:
    std::unique_ptr<SessionPool> pool_;

    // Helper to check if the table already exists (by attempting to query it)
    bool tableExists();
};