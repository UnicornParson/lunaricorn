#include "event_data_extended_type_handler.h"
#include <Poco/Data/Statement.h>
#include <Poco/Data/RecordSet.h>
#include <Poco/Data/Column.h>
#include <Poco/Dynamic/Var.h>
#include <Poco/Exception.h>
#include <boost/json.hpp>
#include <iostream>

namespace Poco {
namespace Data {

std::size_t TypeHandler<lunaricorn::EventDataExtended>::size()
{
    return 7; // eid, type, payload, affected, ctime, owner, tags
}

void TypeHandler<lunaricorn::EventDataExtended>::extract(std::size_t pos, const Poco::Data::RecordSet& rs, lunaricorn::EventDataExtended& obj)
{
    // Extract eid - workaround for const RecordSet issue
    Poco::Dynamic::Var var_eid = rs.value(pos + 0, 0);
    obj.eid = var_eid.convert<int64_t>();
    
    // Extract event_type
    Poco::Dynamic::Var var_event_type = rs.value(pos + 1, 0);
    obj.event_type = var_event_type.convert<std::string>();
    
    // Extract payload as JSON string and parse it
    Poco::Dynamic::Var var_payload = rs.value(pos + 2, 0);
    std::string payloadJson = var_payload.convert<std::string>();
    if (!payloadJson.empty()) {
        try {
            obj.payload = boost::json::parse(payloadJson).as_object();
        } catch (...) {
            // If parsing fails, use empty object
            obj.payload = {};
        }
    } else {
        obj.payload = {};
    }
    
    // Extract affected as JSON string and parse it
    Poco::Dynamic::Var var_affected = rs.value(pos + 3, 0);
    std::string affectedJson = var_affected.convert<std::string>();
    if (!affectedJson.empty() && affectedJson != "null") {
        try {
            boost::json::value affectedValue = boost::json::parse(affectedJson);
            if (affectedValue.is_array()) {
                obj.affected.clear();
            for (const auto& item : affectedValue.as_array()) {
                    if (item.is_string()) {
                        obj.affected.emplace_back(item.as_string().data(), item.as_string().size());
                    }
                }
            }
        } catch (...) {
            // If parsing fails, keep empty affected array
            obj.affected.clear();
        }
    } else {
        obj.affected.clear();
    }
    
    // Extract timestamp
    try {
        Poco::Dynamic::Var var_timestamp = rs.value(pos + 4, 0);
        Poco::DateTime ctime = var_timestamp.convert<Poco::DateTime>();
        obj.timestamp = static_cast<double>(ctime.timestamp().epochMicroseconds()) / 1000000.0;
    } catch (const Poco::Exception& e) {
        // If timestamp conversion fails, use current time as fallback
        obj.timestamp = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    }
    
    // Extract owner/source
    Poco::Dynamic::Var var_owner = rs.value(pos + 5, 0);
    std::string owner = var_owner.convert<std::string>();
    if (owner != "ownerless") {
        obj.source = owner;
    } else {
        obj.source.clear();
    }
    
    // Extract tags - extract as string and parse PostgreSQL array syntax
    Poco::Dynamic::Var var_tags = rs.value(pos + 6, 0);
    std::string tagsStr = var_tags.convert<std::string>();
    if (!tagsStr.empty() && tagsStr != "{}") {
        // Parse PostgreSQL array format: "{tag1,tag2,tag3}"
        obj.tags.clear();
        if (tagsStr.front() == '{' && tagsStr.back() == '}') {
            std::string content = tagsStr.substr(1, tagsStr.length() - 2);
            if (!content.empty()) {
                // Split by comma and handle quoted strings
                size_t start = 0;
                size_t end = 0;
                bool inQuotes = false;
                char quoteChar = '\0';
                
                while (end <= content.length()) {
                    if (end < content.length() && (content[end] == '"' || content[end] == '\'')) {
                        if (!inQuotes) {
                            inQuotes = true;
                            quoteChar = content[end];
                            start = end + 1;
                        } else if (content[end] == quoteChar) {
                            inQuotes = false;
                            std::string tag = content.substr(start, end - start);
                            obj.tags.push_back(tag);
                            // Skip to next comma or end
                            while (end < content.length() && content[end] != ',') end++;
                            start = end + 1;
                        }
                    } else if (!inQuotes && end < content.length() && content[end] == ',') {
                        std::string tag = content.substr(start, end - start);
                        obj.tags.push_back(tag);
                        start = end + 1;
                    }
                    end++;
                }
                
                // Add last element if not empty
                if (start < content.length()) {
                    std::string tag = content.substr(start);
                    obj.tags.push_back(tag);
                }
            }
        }
    } else {
        obj.tags.clear();
    }
}

void TypeHandler<lunaricorn::EventDataExtended>::bind(std::size_t pos, const lunaricorn::EventDataExtended& obj, Poco::Data::Statement& stmt)
{
    // Bind eid (should be auto-generated by PostgreSQL)
    // We don't bind this as it's a BIGSERIAL field
    
    // Bind type
    stmt.bind(pos + 1, obj.event_type);
    
    // Convert payload to JSON string
    std::string payloadJson = boost::json::serialize(obj.payload);
    stmt.bind(pos + 2, payloadJson);
    
    // Convert affected vector to JSON array string
    if (!obj.affected.empty()) {
        boost::json::array affectedArray;
        for (const auto& item : obj.affected) {
            affectedArray.emplace_back(item);
        }
        std::string affectedJson = boost::json::serialize(affectedArray);
        stmt.bind(pos + 3, affectedJson);
    } else {
        stmt.bind(pos + 3, std::string("[]"));
    }
    
    // Bind timestamp
    Poco::DateTime ctime(static_cast<std::time_t>(obj.timestamp));
    stmt.bind(pos + 4, ctime);
    
    // Bind owner/source (use "ownerless" if empty)
    std::string owner = obj.source.empty() ? "ownerless" : obj.source;
    stmt.bind(pos + 5, owner);
    
    // Convert tags vector to PostgreSQL array format
    if (!obj.tags.empty()) {
        std::string tagsStr = "{";
        for (size_t i = 0; i < obj.tags.size(); ++i) {
            if (i > 0) tagsStr += ",";
            // Escape any special characters in the tag string
            std::string escaped_tag = obj.tags[i];
            // Simple escaping - replace " with \" for PostgreSQL compatibility
            size_t pos = 0;
            while ((pos = escaped_tag.find('"', pos)) != std::string::npos) {
                escaped_tag.replace(pos, 1, "\\\"");
                pos += 2;
            }
            tagsStr += "\"" + escaped_tag + "\"";
        }
        tagsStr += "}";
        stmt.bind(pos + 6, tagsStr);
    } else {
        stmt.bind(pos + 6, std::string("{}"));
    }
}

void TypeHandler<lunaricorn::EventDataExtended>::prepare(std::size_t pos, const lunaricorn::EventDataExtended& obj, Poco::Data::Statement& stmt)
{
    // Prepare the statement for binding
    // This is called before bind() and is used to prepare the statement for parameter binding
    
    // Note: In most cases, prepare is not needed when using bind directly.
    // For PostgreSQL with Poco, this function can often be empty.
}

} // namespace Data
} // namespace Poco