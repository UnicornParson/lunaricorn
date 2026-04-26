#include "storage.h"
#include <format>
#include <lunaricorn.h>
#include <Poco/Data/Transaction.h>
using namespace Poco::Data::Keywords;
using namespace lunaricorn;

PGStorage::PGStorage(const DbConfig& config)
{
    if (!config.valid()) {
        throw std::invalid_argument("Invalid database configuration");
    }

    // Build PostgreSQL connection string
    if (config.dbType != "postgresql") {
        throw std::invalid_argument("Unsupported database type: " + config.dbType);
    }
    std::string connStr = "host=" + config.dbHost +
                            " port=" + config.dbPort +
                            " user=" + config.dbUser +
                            " password=" + config.dbPassword +
                            " dbname=" + config.dbDbname;

    // Ensure the PostgreSQL connector is registered (safe to call multiple times)
    static bool registered = false;
    if (!registered) {
        Poco::Data::PostgreSQL::Connector::registerConnector();
        registered = true;
    }

    // Create a session pool (minimum 1, maximum 10 connections)
    pool_ = std::make_unique<Poco::Data::SessionPool>(
        Poco::Data::PostgreSQL::Connector::KEY, connStr, 1, 10);

    testConnection();
    if (!tableExists()) {
        install();
    }
}

// Test the connection by executing a simple query
void PGStorage::testConnection()
{
    try {
        std::lock_guard<std::mutex> lock(pool_mutex); 
        Poco::Data::Session session(pool_->get());
        session << "SELECT 1", now;
    } catch (const Poco::Exception& e) {
        throw std::runtime_error("Failed to connect to database: " + e.displayText());
    }
}

// Create the table and indexes if they do not exist
bool PGStorage::install()
{
    try {
        std::lock_guard<std::mutex> lock(pool_mutex); 
        Poco::Data::Session session(pool_->get());

        // Create table
        session << "CREATE TABLE IF NOT EXISTS maintenance_log ("
                    "o BIGSERIAL PRIMARY KEY, "
                    "owner VARCHAR(256) NOT NULL, "
                    "token VARCHAR(256) NOT NULL, "
                    "timestamputc TIMESTAMP WITHOUT TIME ZONE NOT NULL DEFAULT CURRENT_TIMESTAMP, "
                    "msg TEXT NOT NULL)", now;

        // Create indexes (matching Python SQLAlchemy index=True)
        session << "CREATE INDEX IF NOT EXISTS idx_maintenance_log_owner ON maintenance_log(owner)", now;
        session << "CREATE INDEX IF NOT EXISTS idx_maintenance_log_token ON maintenance_log(token)", now;
        session << "CREATE INDEX IF NOT EXISTS idx_maintenance_log_timestamputc ON maintenance_log(timestamputc)", now;

        std::cout << "Table 'maintenance_log' and indexes created successfully" << std::endl;
        return true;
    } catch (const Poco::Exception& e) {
        std::cerr << "Failed to create table: " << e.displayText() << std::endl;
        return false;
    }
}

std::optional<Poco::Int64> PGStorage::push_all(std::queue<MaintenanceLogRecord>& events)
{
    std::lock_guard<std::mutex> lock(pool_mutex); 
    if (events.empty())
    {
        
        return {last_offset_};
    }
    Poco::Data::Session session(pool_->get());

    Poco::Data::Transaction tr(session);
    try 
    {   uint64_t row_count = 0;
        static uint64_t row_count_total = 0;
        Poco::Int64 maxOffset = 0;
        while (!events.empty())
        {
            
            auto e = events.front();
            events.pop();
            Poco::DateTime now;
            Poco::Int64 newOffset = 0;
            session << "INSERT INTO maintenance_log (owner, token, timestamputc, msg) VALUES ($1, $2, $3, $4) RETURNING o",
            use(e.owner), use(e.token), use(now), use(e.msg),
            into(newOffset), Poco::Data::Keywords::now;
            maxOffset = std::max(maxOffset, newOffset);
            ++row_count;
        }
        tr.commit();
        row_count_total+=row_count;
        std::cout << "@@ commit ok " << row_count << " rows. total:" << row_count_total << std::endl;
    }
    catch (const Poco::Exception& e) {
        tr.rollback();
        std::cerr << "Failed to insert record(1): " << e.displayText() << std::endl;
        return std::nullopt;
    }
    catch (const std::exception& e) {
        tr.rollback();
        std::cerr << "Failed to insert record(2): " << e.what() << std::endl;
        return std::nullopt;
    }
    return {0};
}


std::optional<Poco::Int64> PGStorage::push(const std::string& owner, const std::string& token, const std::string& msg, uint64_t counter)
{
    LOG_ACCESS(owner << " [" << token << "] - " << msg);
    try {
        std::string ownerTrunc = owner.substr(0, 256);
        std::string tokenTrunc = token.substr(0, 256);
        std::stringstream msg_ss;
        msg_ss << "("<<counter<<")" << msg;
        std::string msgCopy = msg_ss.str();   // non-const copy for use()
        Poco::Int64 newOffset = 0;
        Poco::DateTime now;

        std::lock_guard<std::mutex> lock(pool_mutex); 
        static std::chrono::steady_clock::time_point lastTime = std::chrono::steady_clock::now();
        static size_t counter = 0;
        Poco::Data::Session session(pool_->get());
        session << "INSERT INTO maintenance_log (owner, token, timestamputc, msg) VALUES ($1, $2, $3, $4) RETURNING o",
            use(ownerTrunc), use(tokenTrunc), use(now), use(msgCopy),
            into(newOffset), Poco::Data::Keywords::now;
        ++counter;
        auto nowTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(nowTime - lastTime).count();
        if (elapsed >= 1) {
            std::cout << "Transactions per second: " << counter << std::endl;
            counter = 0;
            lastTime = nowTime;
        }
        last_offset_ = newOffset;
        return newOffset;
    } catch (const Poco::Exception& e) {
        std::cerr << "Failed to insert record: " << e.displayText() << std::endl;
        return std::nullopt;
    }
}

// Retrieve records with offset > given value, ordered by offset.
std::vector<MaintenanceLogRecord> PGStorage::pull(Poco::Int64 offset, std::optional<int> limit)
{
    std::vector<MaintenanceLogRecord> result;
    try
    {
        std::lock_guard<std::mutex> lock(pool_mutex); 
        Poco::Data::Session session(pool_->get());
        Poco::Data::Statement stmt(session);

        std::string sql = "SELECT o, owner, token, timestamputc, msg FROM maintenance_log "
                          "WHERE o >= $1 ORDER BY o";
        if (limit.has_value() && limit.value() > 0) {
            sql += " LIMIT $2";
            Poco::Data::Statement stmt(session);
            stmt << sql, use(offset), use(limit.value()), into(result);
            stmt.execute();
        } else {
            Poco::Data::Statement stmt(session);
            stmt << sql, use(offset), into(result);
            stmt.execute();
        }
    }
    catch (const Poco::Exception& e)
    {
        std::cerr << "Failed to fetch records: " << e.displayText() << std::endl;
    }
    return result;
}

// Get all records (offset > -1 effectively means all)
std::vector<MaintenanceLogRecord> PGStorage::getAll()
{
    return pull(-1);
}

// Get a single record by its offset.
std::optional<MaintenanceLogRecord> PGStorage::getByOffset(Poco::Int64 offset)
{
    try
    {
        std::lock_guard<std::mutex> lock(pool_mutex); 
        Poco::Data::Session session(pool_->get());
        MaintenanceLogRecord rec;
        Poco::Data::Statement stmt(session);
        stmt << "SELECT o, owner, token, timestamputc, msg FROM maintenance_log WHERE o = $1",
            use(offset),
            into(rec.offset),
            into(rec.owner),
            into(rec.token),
            into(rec.timestamputc),
            into(rec.msg);

        std::size_t rows = stmt.execute();
        if (rows == 0)
            return std::nullopt;
        return rec;
    }
    catch (const Poco::Exception& e)
    {
        std::cerr << "Failed to fetch record: " << e.displayText() << std::endl;
        return std::nullopt;
    }
}

// Count total records in the table.
std::size_t PGStorage::countRecords()
{
    try {
        std::lock_guard<std::mutex> lock(pool_mutex); 
        Poco::Data::Session session(pool_->get());
        std::size_t count = 0;
        session << "SELECT COUNT(o) FROM maintenance_log", into(count), now;
        return count;
    } catch (const Poco::Exception& e) {
        std::cerr << "Failed to count records: " << e.displayText() << std::endl;
        return 0;
    }
}

// Delete a record by offset. Returns true if deleted, false if not found or error.
bool PGStorage::deleteByOffset(Poco::Int64 offset)
{
    try
    {
        std::lock_guard<std::mutex> lock(pool_mutex); 
        Poco::Data::Session session(pool_->get());
        Poco::Data::Statement stmt(session);
        stmt << "DELETE FROM maintenance_log WHERE o = %1", use(offset);
        Poco::Int64 rows = stmt.execute();
        return rows > 0;
    }
    catch (const Poco::Exception& e)
    {
        std::cerr << "Failed to delete record: " << e.displayText() << std::endl;
        return false;
    }
}

// Helper to check if the table already exists (by attempting to query it)
bool PGStorage::tableExists()
{
    try {
        std::lock_guard<std::mutex> lock(pool_mutex); 
        Poco::Data::Session session(pool_->get());
        session << "SELECT 1 FROM maintenance_log LIMIT 1", now;
        return true;
    } catch (const Poco::Exception&) {
        return false;
    }
}
