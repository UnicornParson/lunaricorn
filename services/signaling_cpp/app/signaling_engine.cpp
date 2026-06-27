#include "signaling_engine.h"
#include <stdexcept>

namespace lunaricorn {

SignalingEngine::SignalingEngine(const DbConfig& db_cfg)
{
    try {
        storage_ = std::make_unique<MessageStorage>(db_cfg);
        MLOG("SignalingEngine initialized with MessageStorage");
    } catch (const std::exception& e) {
        MLOG_E("Failed to initialize MessageStorage: {}", e.what());
        throw;
    }
}

SignalingEngine::~SignalingEngine()
{
}

long long SignalingEngine::createEvent(const StoredEventData& event_data)
{
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        return storage_->create_event(event_data);
    } catch (const std::exception& e) {
        MLOG_E("Failed to create event: {}", e.what());
        throw;
    }
}

std::vector<std::string> SignalingEngine::getUniqueValues(const std::string& field_name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        return storage_->get_unique_values(field_name);
    } catch (const std::exception& e) {
        MLOG_E("Failed to get unique values for field '{}': {}", field_name, e.what());
        throw;
    }
}

std::vector<StoredEventDataExtended> SignalingEngine::findEvents(
    double timestamp,
    const std::vector<std::string>& types,
    const std::vector<std::string>& sources,
    const std::vector<std::string>& affected,
    const std::vector<std::string>& tags,
    int limit)
{
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        return storage_->find_events(timestamp, types, sources, affected, tags, limit);
    } catch (const std::exception& e) {
        MLOG_E("Failed to find events: {}", e.what());
        throw;
    }
}

std::vector<StoredEventDataExtended> SignalingEngine::findEventsByType(const std::string& event_type)
{
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        return storage_->find_events_by_type(event_type);
    } catch (const std::exception& e) {
        MLOG_E("Failed to find events by type '{}': {}", event_type, e.what());
        throw;
    }
}

} // namespace lunaricorn