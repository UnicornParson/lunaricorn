#include "storage.h"
using namespace Poco::Data::Keywords;


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
    pool_ = std::make_unique<SessionPool>(Poco::Data::PostgreSQL::Connector::KEY, connStr, 1, 10);

    testConnection();
    if (!tableExists()) {
        install();
    }
}

// Test the connection by executing a simple query
void PGStorage::testConnection()
{
    try {
        Session session(pool_->get());
        session << "SELECT 1", now;
    } catch (const Exception& e) {
        throw std::runtime_error("Failed to connect to database: " + e.displayText());
    }
}

// Create the table and indexes if they do not exist
bool PGStorage::install()
{
    try {
        Session session(pool_->get());

        // Create table
        session << "CREATE TABLE IF NOT EXISTS maintenance_log ("
                    "offset BIGSERIAL PRIMARY KEY, "
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
    } catch (const Exception& e) {
        std::cerr << "Failed to create table: " << e.displayText() << std::endl;
        return false;
    }
}

// Insert a new record. Returns the generated offset, or std::nullopt on failure.
std::optional<Poco::Int64> PGStorage::push(const std::string& owner, const std::string& token, const std::string& msg)
{
    try {
        std::string ownerTrunc = owner.substr(0, 256);
        std::string tokenTrunc = token.substr(0, 256);
        std::string msgCopy = msg;   // non-const copy for use()

        Session session(pool_->get());
        Poco::Int64 newOffset = 0;

        session << "INSERT INTO maintenance_log (owner, token, timestamputc, msg) "
                   "VALUES (?, ?, CURRENT_TIMESTAMP, ?) RETURNING offset",
            use(ownerTrunc), use(tokenTrunc), use(msgCopy),
            into(newOffset), now;

        return newOffset;
    } catch (const Exception& e) {
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
        Poco::Data::Session session(pool_->get());
        Poco::Data::Statement stmt(session);
        MaintenanceLogRecord rec;

        // Формируем SQL с учётом лимита
        std::string sql = "SELECT offset, owner, token, timestamputc, msg FROM maintenance_log "
                          "WHERE offset >= ? ORDER BY offset";
        if (limit.has_value() && limit.value() > 0)
        {
            sql += " LIMIT ?";
            // При наличии лимита добавляем его в список параметров
            stmt << sql,
                use(offset),
                use(limit.value()),
                into(rec.offset),
                into(rec.owner),
                into(rec.token),
                into(rec.timestamputc),
                into(rec.msg);
        } else {
            stmt << sql,
                use(offset),
                into(rec.offset),
                into(rec.owner),
                into(rec.token),
                into(rec.timestamputc),
                into(rec.msg);
        }

        while (!stmt.done()) {
            if (stmt.execute() > 0) {
                result.push_back(rec);
            }
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
        Session session(pool_->get());
        MaintenanceLogRecord rec;
        Statement stmt(session);
        stmt << "SELECT offset, owner, token, timestamputc, msg FROM maintenance_log WHERE offset = ?",
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
    catch (const Exception& e)
    {
        std::cerr << "Failed to fetch record: " << e.displayText() << std::endl;
        return std::nullopt;
    }
}

// Count total records in the table.
std::size_t PGStorage::countRecords()
{
    try {
        Session session(pool_->get());
        std::size_t count = 0;
        session << "SELECT COUNT(*) FROM maintenance_log", into(count), now;
        return count;
    } catch (const Exception& e) {
        std::cerr << "Failed to count records: " << e.displayText() << std::endl;
        return 0;
    }
}

// Delete a record by offset. Returns true if deleted, false if not found or error.
bool PGStorage::deleteByOffset(Poco::Int64 offset)
{
    try
    {
        Session session(pool_->get());
        Statement stmt(session);
        stmt << "DELETE FROM maintenance_log WHERE offset = ?", use(offset);
        Poco::Int64 rows = stmt.execute();
        return rows > 0;
    }
    catch (const Exception& e)
    {
        std::cerr << "Failed to delete record: " << e.displayText() << std::endl;
        return false;
    }
}

// Helper to check if the table already exists (by attempting to query it)
bool PGStorage::tableExists()
{
    try {
        Session session(pool_->get());
        session << "SELECT 1 FROM maintenance_log LIMIT 1", now;
        return true;
    } catch (const Exception&) {
        return false;
    }
}
