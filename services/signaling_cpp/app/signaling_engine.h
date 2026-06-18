#pragma once

#include <string>
#include <vector>
#include <memory>
#include <Poco/Logger.h>
#include <Poco/SharedPtr.h>

// Forward declarations for data types
class EventData;
class EventDataExtended;
struct DbConfig;

namespace lunaricorn {

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
    std::vector<EventDataExtended> resultToEventDataExtendedList(
        const std::vector<std::vector<Poco::Dynamic::Var>>& data_list);
    
    DbConfig db_cfg_;
    bool ready_;
    bool db_enabled_;
    Poco::Logger& logger_;
    std::shared_ptr<Poco::NotificationCenter> notification_center_;
    std::unique_ptr<Poco::Data::SessionPool> pool_;
    std::mutex pool_mutex_;
};

} // namespace lunaricorn