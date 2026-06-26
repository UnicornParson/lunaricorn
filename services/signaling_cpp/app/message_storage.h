#pragma once

#include <soci/soci.h>
#include <soci/postgresql/soci-postgresql.h>

#include <boost/json.hpp>

#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <stdexcept>

#include <lunaricorn.h>

namespace json = boost::json;

namespace lunaricorn
{

struct EventData
{
    std::string event_type;
    json::value payload;
    double timestamp;
    std::optional<std::string> source;
    json::value affected;
    std::vector<std::string> tags;
};



struct EventDataExtended : EventData
{
    long long eid;
};



class BrokenStorageError : public std::runtime_error
{
public:
    explicit BrokenStorageError(const std::string& msg):std::runtime_error(msg){}
};



class MessageStorage
{

public:
    explicit MessageStorage(const DbConfig& cfg);
    long long create_event(const EventData& event);
    std::vector<std::string> get_unique_values(const std::string& field);

    std::vector<EventDataExtended>
    find_events(
        double timestamp,
        const std::vector<std::string>& types = {},
        const std::vector<std::string>& sources = {},
        const std::vector<std::string>& affected = {},
        const std::vector<std::string>& tags = {},
        int limit = 0
    );
    std::vector<EventDataExtended>
    find_events_by_type(const std::string& type);

private:
    soci::session sql;
    EventDataExtended
    row_to_event(soci::row& row);
    std::string json_to_string(const json::value& v);
}; // class MessageStorage

} // namespace lunaricorn