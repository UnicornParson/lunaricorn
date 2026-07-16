#include "blob_storage.h"

#include <sstream>

BlobStorage::BlobStorage(const lunaricorn::DbConfig& cfg)
{
    if(!cfg.valid()) {
        throw std::invalid_argument("invalid db config");
    }

    try {
        std::ostringstream conn;
        conn << "host=" << cfg.dbHost
             << " port=" << cfg.dbPort
             << " user=" << cfg.dbUser
             << " password=" << cfg.dbPassword
             << " dbname=" << cfg.dbDbname;
        sql.open(soci::postgresql, conn.str());

        sql << R"(
            CREATE TABLE IF NOT EXISTS orb_blob (
                key   VARCHAR(128) PRIMARY KEY,
                value JSONB NOT NULL
            )
        )";

        ok_ = true;
        MLOG_D("BlobStorage: connected, table orb_blob ready");
    } catch(const std::exception& e) {
        ok_ = false;
        MLOG_E("BlobStorage: connection failed: {}", e.what());
        throw;
    }
}

std::string BlobStorage::json_to_string(const json::value& v)
{
    return json::serialize(v);
}

bool BlobStorage::contains(const std::string& id)
{
    if(!ok_) {
        MLOG_E("BlobStorage::contains({}): not ok", id);
        return false;
    }

    try {
        int exists = 0;
        sql << "SELECT 1 FROM orb_blob WHERE key = :id",
            soci::into(exists),
            soci::use(id);
        MLOG_D("BlobStorage::contains({}): {}", id, exists == 1 ? "found" : "not found");
        return exists == 1;
    } catch(const std::exception& e) {
        MLOG_E("BlobStorage::contains({}): exception: {}", id, e.what());
        return false;
    }
}

bool BlobStorage::store(const std::string& id, const json::object& data)
{
    if(!ok_) {
        MLOG_E("BlobStorage::store({}): not ok, skipping", id);
        return false;
    }

    try {
        std::string value = json_to_string(json::value(data));
        MLOG_D("BlobStorage::store({}): value='{}'", id, value);

        sql << R"(
            INSERT INTO orb_blob (key, value)
            VALUES (:id, :val::jsonb)
            ON CONFLICT (key)
            DO UPDATE SET value = :val2::jsonb
        )",
            soci::use(id),
            soci::use(value),
            soci::use(value);

        MLOG_D("BlobStorage::store({}): success", id);
        return true;
    } catch(const std::exception& e) {
        MLOG_E("BlobStorage::store({}): exception: {}", id, e.what());
        return false;
    }
}

std::optional<json::object> BlobStorage::load(const std::string& id)
{
    if(!ok_) {
        MLOG_E("BlobStorage::load({}): not ok", id);
        return std::nullopt;
    }

    try {
        std::string value;
        soci::indicator ind = soci::i_null;
        sql << "SELECT value FROM orb_blob WHERE key = :id",
            soci::into(value, ind),
            soci::use(id);

        if(ind != soci::i_ok) {
            MLOG_D("BlobStorage::load({}): not found", id);
            return std::nullopt;
        }

        json::value parsed = json::parse(value);
        if(!parsed.is_object()) {
            MLOG_E("BlobStorage::load({}): value is not an object: {}", id, value);
            return std::nullopt;
        }

        MLOG_D("BlobStorage::load({}): found value='{}'", id, value);
        return parsed.as_object();
    } catch(const std::exception& e) {
        MLOG_E("BlobStorage::load({}): exception: {}", id, e.what());
        return std::nullopt;
    }
}

size_t BlobStorage::count()
{
    if(!ok_) return 0;

    try {
        size_t cnt = 0;
        sql << "SELECT COUNT(*) FROM orb_blob", soci::into(cnt);
        return cnt;
    } catch(const std::exception&) {
        return 0;
    }
}

bool BlobStorage::ok()
{
    return ok_;
}