#pragma once

#include "stdafx.h"
#include <lunaricorn.h>


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

} // namespace lunaricorn