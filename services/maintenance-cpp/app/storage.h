#pragma once

#include <string>
#include <vector>
#include <optional>
#include <iostream>
#include <memory>
#include <mutex>
#include <Poco/Data/Session.h>
#include <Poco/Data/SessionPool.h>
#include <Poco/Data/PostgreSQL/Connector.h>
#include <Poco/Data/Statement.h>
#include <Poco/Data/RecordSet.h>
#include <Poco/DateTime.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/Exception.h>
#include <Poco/Data/TypeHandler.h>
#include <lunaricorn.h>

namespace lunaricorn
{
struct MaintenanceLogRecord {
    Poco::Int64 offset;
    std::string owner;
    std::string token;
    Poco::DateTime timestamputc;
    std::string msg;
};
} // namespace lunaricorn

namespace Poco 
{
    namespace Data
    {
        template <>
        class TypeHandler<lunaricorn::MaintenanceLogRecord>
        {
        public:
            static std::size_t size()
            {
                return 5; // o, owner, token, timestamputc, msg
            }
            static void bind(std::size_t pos, const lunaricorn::MaintenanceLogRecord& obj, AbstractBinder::Ptr pBinder, AbstractBinder::Direction dir)
            {
                // Если понадобится вставка/обновление — реализуйте здесь
                pBinder->bind(pos, obj.offset);
                pBinder->bind(pos + 1, obj.owner);
                pBinder->bind(pos + 2, obj.token);
                pBinder->bind(pos + 3, obj.timestamputc);
                pBinder->bind(pos + 4, obj.msg);
            }
        
            static void prepare(std::size_t pos, const lunaricorn::MaintenanceLogRecord& obj, AbstractPreparator::Ptr pPrep)
            {
                throw Poco::NotImplementedException("MaintenanceLogRecord does not support binding (prepare)");
            }
        
            static void extract(std::size_t pos, lunaricorn::MaintenanceLogRecord& obj, const lunaricorn::MaintenanceLogRecord& defVal, AbstractExtractor::Ptr pExt)
            {
                if (!pExt->extract(pos, obj.offset)) obj.offset = defVal.offset;
                if (!pExt->extract(pos + 1, obj.owner)) obj.owner = defVal.owner;
                if (!pExt->extract(pos + 2, obj.token)) obj.token = defVal.token;
                if (!pExt->extract(pos + 3, obj.timestamputc)) obj.timestamputc = defVal.timestamputc;
                if (!pExt->extract(pos + 4, obj.msg)) obj.msg = defVal.msg;
            }
        }; // class TypeHandler
    } // namespace Data
} // namespace Poco

namespace lunaricorn
{

class PGStorage
{
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
    std::mutex pool_mutex;

    // Helper to check if the table already exists (by attempting to query it)
    bool tableExists();
}; // class PGStorage

} // namespace lunaricorn