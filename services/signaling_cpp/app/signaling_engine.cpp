#include "signaling_engine.h"

#include <Poco/Data/Session.h>
#include <Poco/Data/SessionPool.h>
#include <Poco/Data/PostgreSQL/Connector.h>
#include <Poco/Data/Statement.h>
#include <Poco/Data/RecordSet.h>
#include <Poco/Data/Transaction.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/DateTimeParser.h>
#include <Poco/Exception.h>
#include <Poco/Logger.h>
#include <Poco/Format.h>
#include <boost/json.hpp>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>

using namespace Poco::Data::Keywords;
using namespace lunaricorn;

namespace lunaricorn {

SignalingEngine::SignalingEngine(const DbConfig& db_cfg)
    : db_cfg_(db_cfg),
      ready_(false),
      db_enabled_(false),
      logger_(Poco::Logger::get("signaling_engine"))
{
    if (!db_cfg_.valid()) {
        throw std::invalid_argument("Invalid database configuration");
    }
    
    try {
        initializeDatabase();
    } catch (const std::exception& e) {
        logger_.error(Poco::format("Failed to initialize database connection: %s", e.what()));
        throw;
    }
}

SignalingEngine::~SignalingEngine()
{
}

void SignalingEngine::initializeDatabase()
{
    // Ensure the PostgreSQL connector is registered (safe to call multiple times)
    static bool registered = false;
    if (!registered) {
        Poco::Data::PostgreSQL::Connector::registerConnector();
        registered = true;
    }

    // Build PostgreSQL connection string
    std::string connStr = "host=" + db_cfg_.dbHost +
                          " port=" + db_cfg_.dbPort +
                          " user=" + db_cfg_.dbUser +
                          " password=" + db_cfg_.dbPassword +
                          " dbname=" + db_cfg_.dbDbname;

    // Create a session pool (minimum 1, maximum 3 connections to match Python implementation)
    pool_ = std::make_unique<Poco::Data::SessionPool>(
        Poco::Data::PostgreSQL::Connector::KEY, connStr, 1, 3);

    // Test the connection
    testConnection();
    
    // Install database schema if needed
    installDb();
    
    ready_ = true;
    db_enabled_ = true;
    logger_.information("Database connection initialized for signaling service");
}

void SignalingEngine::testConnection()
{
    try {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        Poco::Data::Session session(pool_->get());
        session << "SELECT 1", now;
    } catch (const Poco::Exception& e) {
        throw std::runtime_error("Failed to connect to database: " + e.displayText());
    }
}

void SignalingEngine::installDb()
{
    try {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        Poco::Data::Session session(pool_->get());

        // Create the signaling_events table if it doesn't exist
        session << R"(
            CREATE TABLE IF NOT EXISTS public.signaling_events (
                eid BIGSERIAL PRIMARY KEY,
                type VARCHAR(255) NOT NULL,
                payload JSONB,
                affected JSONB,
                ctime TIMESTAMP WITHOUT TIME ZONE NOT NULL DEFAULT CURRENT_TIMESTAMP,
                owner VARCHAR(255),
                tags TEXT[]
            )
        )", now;

        // Create indexes for performance
        session << "CREATE INDEX IF NOT EXISTS idx_signaling_events_type ON public.signaling_events(type)", now;
        session << "CREATE INDEX IF NOT EXISTS idx_signaling_events_owner ON public.signaling_events(owner)", now;
        session << "CREATE INDEX IF NOT EXISTS idx_signaling_events_ctime ON public.signaling_events(ctime)", now;
        session << "CREATE INDEX IF NOT EXISTS idx_signaling_events_tags ON public.signaling_events USING GIN(tags)", now;
        
        logger_.information("Database schema installed for signaling service");
    } catch (const Poco::Exception& e) {
        logger_.error(Poco::format("Failed to install database schema: %s", e.displayText()));
        throw std::runtime_error("Failed to install database schema: " + e.displayText());
    }
}

int SignalingEngine::createEvent(const EventData& event_data)
{
    try {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        
        Poco::Data::Session session(pool_->get());
        Poco::Data::Transaction tr(session);
        
        // Convert timestamp to Poco::DateTime
        Poco::DateTime ctime = event_data.timestamp;
        
        // Handle owner (use "ownerless" if source is empty)
        std::string owner = event_data.source.empty() ? "ownerless" : event_data.source;
        
        // Convert payload and affected to JSON strings
        boost::json::object payload_json_obj = event_data.payload;
        std::string payload_json_str = "";
        if (!payload_json_obj.empty()) {
            payload_json_str = boost::json::serialize(payload_json_obj);
        }
        
        std::string affected_json_str = "";
        if (!event_data.affected.empty()) {
            boost::json::array affected_array;
            for (const auto& item : event_data.affected) {
                affected_array.emplace_back(item);
            }
            affected_json_str = boost::json::serialize(affected_array);
        }
        
        // Prepare parameters for the query
        std::string event_type = event_data.event_type;
        std::string tags_str = ""; // Tags are stored as text array in PostgreSQL
        
        int eid = 0;
        
        session << R"(
            INSERT INTO public.signaling_events 
            (type, payload, affected, ctime, owner, tags)
            VALUES ($1, $2, $3, $4, $5, $6)
            RETURNING eid
        )",
            use(event_type), use(payload_json_str), use(affected_json_str), use(ctime), use(owner), use(tags_str),
            into(eid), now;
        
        tr.commit();
        return eid;
    } catch (const Poco::Exception& e) {
        logger_.error(Poco::format("Failed to create event: %s", e.displayText()));
        throw StorageError("Failed to create event: " + e.displayText());
    }
}

std::vector<std::string> SignalingEngine::getUniqueValues(const std::string& field_name)
{
    if (field_name != "type" && field_name != "affected" && 
        field_name != "owner" && field_name != "tags") {
        throw std::invalid_argument("list operation for field " + field_name + " not supported");
    }

    try {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        Poco::Data::Session session(pool_->get());
        
        std::vector<std::string> result;
        
        if (field_name == "type" || field_name == "owner") {
            // For type and owner, we can use simple DISTINCT query
            std::string sql = "SELECT DISTINCT " + field_name + " as f "
                             "FROM public.signaling_events "
                             "WHERE " + field_name + " IS NOT NULL "
                             "ORDER BY f ASC";
            
            Poco::Data::Statement stmt(session);
            stmt << sql, into(result);
            stmt.execute();
        }
        else if (field_name == "tags") {
            // For tags, we need to use UNNEST with array
            std::string sql = R"(
                SELECT DISTINCT UNNEST(tags) as tag
                FROM public.signaling_events
                WHERE tags IS NOT NULL AND array_length(tags, 1) > 0
                ORDER BY tag ASC
            )";
            
            Poco::Data::Statement stmt(session);
            stmt << sql, into(result);
            stmt.execute();
        }
        else if (field_name == "affected") {
            // For affected, we need to use jsonb_array_elements_text
            std::string sql = R"(
                SELECT DISTINCT jsonb_array_elements_text(affected) AS affected_value
                FROM public.signaling_events
                WHERE affected IS NOT NULL 
                AND affected != 'null'::jsonb
                AND jsonb_typeof(affected) = 'array'
                ORDER BY affected_value ASC
            )";
            
            Poco::Data::Statement stmt(session);
            stmt << sql, into(result);
            stmt.execute();
        }
        
        return result;
    } catch (const Poco::Exception& e) {
        logger_.error(Poco::format("Failed to get unique values for field %s: %s", field_name, e.displayText()));
        throw StorageError("Failed to get unique values: " + e.displayText());
    }
}

std::vector<EventDataExtended> SignalingEngine::findEvents(double timestamp,
                                                          const std::vector<std::string>& types,
                                                          const std::vector<std::string>& sources,
                                                          const std::vector<std::string>& affected,
                                                          const std::vector<std::string>& tags,
                                                          int limit)
{
    try {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        Poco::Data::Session session(pool_->get());
        
        // Build the base query
        std::string sql = R"(
            SELECT eid, type, payload, affected, ctime, owner, tags
            FROM public.signaling_events 
            WHERE ctime >= $1
        )";
        
        // Add conditions for types
        if (!types.empty()) {
            sql += " AND type IN (";
            for (size_t i = 0; i < types.size(); ++i) {
                sql += "$" + std::to_string(i + 2); // Parameters start from $2 since $1 is timestamp
                if (i < types.size() - 1) sql += ", ";
            }
            sql += ")";
        }
        
        // Add conditions for sources
        if (!sources.empty()) {
            sql += " AND owner IN (";
            for (size_t i = 0; i < sources.size(); ++i) {
                sql += "$" + std::to_string(types.size() + i + 2);
                if (i < sources.size() - 1) sql += ", ";
            }
            sql += ")";
        }
        
        // Add conditions for affected
        if (!affected.empty()) {
            sql += " AND affected @> ANY(ARRAY[";
            for (size_t i = 0; i < affected.size(); ++i) {
                sql += "$" + std::to_string(types.size() + sources.size() + i + 2);
                if (i < affected.size() - 1) sql += ", ";
            }
            sql += "]::jsonb[])";
        }
        
        // Add conditions for tags
        if (!tags.empty()) {
            sql += " AND tags && ARRAY[";
            for (size_t i = 0; i < tags.size(); ++i) {
                sql += "$" + std::to_string(types.size() + sources.size() + affected.size() + i + 2);
                if (i < tags.size() - 1) sql += ", ";
            }
            sql += "]";
        }
        
        // Add order by
        sql += " ORDER BY ctime DESC";
        
        // Add limit if specified
        if (limit > 0) {
            sql += " LIMIT " + std::to_string(limit);
        }
        
        // Prepare the parameters
        std::vector<Poco::Dynamic::Var> params;
        params.push_back(Poco::DateTime(static_cast<std::time_t>(timestamp)));
        
        // Add type parameters
        for (const auto& type : types) {
            params.push_back(type);
        }
        
        // Add source parameters
        for (const auto& source : sources) {
            params.push_back(source);
        }
        
        // Add affected parameters (need to wrap in JSON array)
        for (const auto& a : affected) {
            std::string json_array = "[\"" + a + "\"]";
            params.push_back(json_array);
        }
        
        // Add tag parameters
        for (const auto& tag : tags) {
            params.push_back(tag);
        }
        
        // Execute query
        Poco::Data::Statement stmt(session);
        std::vector<std::vector<Poco::Dynamic::Var>> result_rows;
        stmt << sql, use(params), into(result_rows);
        stmt.execute();
        
        // Convert results to EventDataExtended objects
        return resultToEventDataExtendedList(result_rows);
    } catch (const Poco::Exception& e) {
        logger_.error(Poco::format("Failed to find events: %s", e.displayText()));
        throw StorageError("Failed to find events: " + e.displayText());
    }
}

std::vector<EventDataExtended> SignalingEngine::findEventsByType(const std::string& event_type)
    {
        try {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            Poco::Data::Session session(pool_->get());
            
            // Build the query
            std::string sql = R"(
                SELECT eid, type, payload, affected, ctime, owner, tags
                FROM public.signaling_events 
                WHERE type = $1
                ORDER BY ctime DESC
            )";
            
            Poco::Data::Statement stmt(session);
            std::vector<std::vector<Poco::Dynamic::Var>> result_rows;
            // Create a non-const copy to avoid Poco const reference issue
            std::string event_type_copy = event_type;
            stmt << sql, use(event_type_copy), into(result_rows);
            stmt.execute();
            
            // Convert results to EventDataExtended objects
            return resultToEventDataExtendedList(result_rows);
        } catch (const Poco::Exception& e) {
            logger_.error(Poco::format("Failed to find events by type '%s': %s", event_type, e.displayText()));
            throw StorageError("Failed to find events by type: " + e.displayText());
        }
    }

std::vector<EventDataExtended> SignalingEngine::resultToEventDataExtendedList(
    const std::vector<std::vector<Poco::Dynamic::Var>>& data_list)
{
    std::vector<EventDataExtended> result;
    
    for (const auto& row : data_list) {
        if (row.size() < 7) continue; // Skip malformed rows
        
        try {
            EventDataExtended event_data;
            
            // Extract and convert values
            event_data.eid = row[0].convert<int>();
            event_data.event_type = row[1].convert<std::string>();
            
            // Handle payload (JSON)
            std::string payload_str = row[2].convert<std::string>();
            if (!payload_str.empty()) {
                boost::json::value payload_value = boost::json::parse(payload_str);
                if (payload_value.is_object()) {
                    event_data.payload = payload_value.as_object();
                } else {
                    event_data.payload = boost::json::object{};
                }
            } else {
                event_data.payload = boost::json::object{};
            }
            
            // Handle affected (JSON array)
            std::string affected_str = row[3].convert<std::string>();
            if (!affected_str.empty() && affected_str != "null") {
                try {
                    boost::json::value affected_value = boost::json::parse(affected_str);
                    if (affected_value.is_array()) {
                        for (const auto& item : affected_value.as_array()) {
                            if (item.is_string()) {
                                event_data.affected.push_back(item.as_string().c_str());
                            }
                        }
                    }
                } catch (...) {
                    // If parsing fails, keep empty affected array
                    event_data.affected.clear();
                }
            }
            
            // Handle timestamp
            Poco::DateTime ctime = row[4].convert<Poco::DateTime>();
            event_data.timestamp = static_cast<double>(ctime.timestamp().epochMicroseconds()) / 1000000.0;
            
            // Handle owner
            std::string owner = row[5].convert<std::string>();
            if (owner != "ownerless") {
                event_data.source = owner;
            } else {
                event_data.source.clear();
            }
            
            // Handle tags - not used in the original Python implementation, so we leave empty
            
            result.push_back(event_data);
        } catch (const Poco::Exception& e) {
            logger_.error(Poco::format("Failed to convert row to EventDataExtended: %s", e.displayText()));
            // Skip malformed rows but continue processing others
            continue;
        }
    }
    
    return result;
}

} // namespace lunaricorn