#include "event_data.h"
#include <boost/json/src.hpp>
#include "stdafx.h"
namespace lunaricorn
{
bool EventData::fromJson(const boost::json::object& obj)
{
    // обязательные поля
    if (!obj.contains("type") || !obj.contains("payload") || !obj.contains("ctime"))
        return false;

    // event_type
    const auto& type_val = obj.at("type");
    if (!type_val.is_string())
        return false;
    event_type = type_val.as_string().c_str();

    // payload (должен быть объектом)
    const auto& payload_val = obj.at("payload");
    if (!payload_val.is_object())
        return false;
    payload = payload_val.as_object();

    // timestamp (число секунд от эпохи или строка)
    const auto& ctime_val = obj.at("ctime");
    if (ctime_val.is_number())
    {
        double seconds = ctime_val.as_double();
        timestamp = Poco::DateTime(static_cast<std::time_t>(seconds));
    }
    else if (ctime_val.is_string())
    {
        std::string s = ctime_val.as_string().c_str();
        int tzd = 0;
        try
        {
            timestamp = Poco::DateTimeParser::parse(
                Poco::DateTimeFormat::ISO8601_FRAC_FORMAT, s, tzd);
            timestamp.makeUTC(tzd);
        }
        catch (...)
        {
            try
            {
                timestamp = Poco::DateTimeParser::parse(
                    "%Y-%m-%dT%H:%M:%S", s, tzd);
                timestamp.makeUTC(tzd);
            }
            catch (...)
            {
                return false;
            }
        }
    }
    else
    {
        return false;
    }

    source.clear();
    if (obj.contains("owner"))
    {
        const auto& owner_val = obj.at("owner");
        if (owner_val.is_string())
            source = owner_val.as_string().c_str();
    }

    affected.clear();
    if (obj.contains("affected"))
    {
        const auto& aff_val = obj.at("affected");
        if (aff_val.is_array())
        {
            for (const auto& item : aff_val.as_array())
                if (item.is_string())
                    affected.push_back(item.as_string().c_str());
        }
    }

    tags.clear();
    if (obj.contains("tags"))
    {
        const auto& tags_val = obj.at("tags");
        if (tags_val.is_array())
        {
            for (const auto& item : tags_val.as_array())
                if (item.is_string())
                    tags.push_back(item.as_string().c_str());
        }
    }

    return true;
}


boost::json::object EventData::toJson() const
{
    boost::json::object obj;
    obj["type"] = event_type;
    obj["payload"] = payload;
    obj["ctime"] = static_cast<double>(timestamp.timestamp().epochMicroseconds()) / 1'000'000.0;
    if (!source.empty())
        obj["owner"] = source;

    if (!affected.empty())
    {
        boost::json::array aff_arr;
        for (const auto& a : affected)
            aff_arr.emplace_back(a);
        obj["affected"] = aff_arr;
    }

    if (!tags.empty())
    {
        boost::json::array tags_arr;
        for (const auto& t : tags)
            tags_arr.emplace_back(t);
        obj["tags"] = tags_arr;
    }

    return obj;
}


} // namespace lunaricorn