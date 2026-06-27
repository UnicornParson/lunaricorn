#pragma once

#include "stdafx.h"
#include <Poco/DateTime.h>
#include <Poco/DateTimeParser.h>
#include <Poco/DateTimeFormat.h>
#include <boost/json.hpp>

namespace lunaricorn
{

struct EventData
{
    std::string event_type;
    boost::json::object payload;
    Poco::DateTime timestamp;
    std::string source;
    std::vector<std::string> affected;
    std::vector<std::string> tags;

    bool fromJson(const boost::json::object& object);
    boost::json::object toJson() const;
};

struct EventDataExtended : EventData
{
    long long eid = 0;
};

} // namespace lunaricorn
