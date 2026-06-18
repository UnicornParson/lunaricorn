#pragma once
#include "stdafx.h"
#include <string>
#include <vector>
#include <memory>
#include <Poco/Logger.h>
#include <Poco/SharedPtr.h>
#include <Poco/Data/SessionPool.h>
#include <Poco/Data/PostgreSQL/Connector.h>
#include <mutex>
#include "event_data.h"

namespace lunaricorn {

// Custom exception class for storage operations
class StorageError : public std::exception {
public:
    explicit StorageError(const std::string& message) : msg_(message) {}
    const char* what() const noexcept override { return msg_.c_str(); }
private:
    std::string msg_;
};

struct EventDataExtended {
    int eid;
    std::string event_type;
    boost::json::object payload;
    Poco::DateTime timestamp;
    std::string source;
    std::vector<std::string> affected;
    std::vector<std::string> tags;
    
    // Default constructor
    EventDataExtended() : eid(0) {}
    
    // Constructor from EventData
    EventDataExtended(const EventData& data) 
        : eid(0), event_type(data.event_type), payload(data.payload), 
          timestamp(data.timestamp), source(data.source), affected(data.affected), tags(data.tags) {}
};

class SignalingEngine
{
public:
    explicit SignalingEngine(const DbConfig& db_cfg);
    ~SignalingEngine();

    int createEvent(const EventData& event_data);
    std::vector<std::string> getUniqueValues(const std::string& field_name);
    std::vector<EventDataExtended> findEvents(double timestamp, 
                                             const std::vector<std::string>& types = {},
                                             const std::vector<std::string>& sources = {},
                                             const std::vector<std::string>& affected = {},
                                             const std::vector<std::string>& tags = {},
                                             int limit = 0);
    std::vector<EventDataExtended> findEventsByType(const std::string& event_type);

private:
    void initializeDatabase();
    void testConnection();
    void installDb();
    std::vector<EventDataExtended> resultToEventDataExtendedList(
        const std::vector<std::vector<Poco::Dynamic::Var>>& data_list);
    
    DbConfig db_cfg_;
    bool ready_;
    bool db_enabled_;
    Poco::Logger& logger_;
    std::unique_ptr<Poco::Data::SessionPool> pool_;
    std::mutex pool_mutex_;
};

} // namespace lunaricorn
