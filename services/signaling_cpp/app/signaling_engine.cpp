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

void SignalingEngine::subscribe(uint64_t client_id,
                                 const std::vector<std::string>& types,
                                 const std::vector<std::string>& sources,
                                 const std::vector<std::string>& affected,
                                 const std::vector<std::string>& tags)
{
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        Subscriber subscriber;
        subscriber.reg_point = std::chrono::system_clock::now();
        subscriber.filter_types = types;
        subscriber.filter_sources = sources;
        subscriber.filter_affected = affected;
        subscriber.filter_tags = tags;
        subscriber.client_id = client_id;
        subscriber.count = 0;

        subscribers_[client_id] = subscriber;
        MLOG("Client {} subscribed to events", client_id);
    } catch (const std::exception& e) {
        MLOG_E("Failed to subscribe client {}: {}", client_id, e.what());
        throw;
    }
}

void SignalingEngine::unsubscribe(uint64_t client_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        auto it = subscribers_.find(client_id);
        if (it != subscribers_.end()) {
            subscribers_.erase(it);
            MLOG("Client {} unsubscribed from events", client_id);
        } else {
            MLOG("Client {} was not subscribed", client_id);
        }
    } catch (const std::exception& e) {
        MLOG_E("Failed to unsubscribe client {}: {}", client_id, e.what());
        throw;
    }
}

void SignalingEngine::setOnSubEvent(std::function<void(uint64_t, const StoredEventData&)> cb)
{
    onSubEvent_ = std::move(cb);
}

void SignalingEngine::on_event(const StoredEventData& event_data)
{
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        for (auto& [client_id, sub] : subscribers_) {
            // Check if subscriber filters match the event
            bool match = true;

            // Check type filter
            if (!sub.filter_types.empty()) {
                match = false;
                for (const auto& t : sub.filter_types) {
                    if (event_data.event_type == t) {
                        match = true;
                        break;
                    }
                }
            }

            // Check source filter
            if (match && !sub.filter_sources.empty()) {
                match = false;
                if (event_data.source.has_value()) {
                    for (const auto& s : sub.filter_sources) {
                        if (*event_data.source == s) {
                            match = true;
                            break;
                        }
                    }
                } else {
                    match = false;
                }
            }

            // Check affected filter
            if (match && !sub.filter_affected.empty()) {
                match = false;
                // Compare affected json value with filter strings
                std::string affected_str = boost::json::value_to<std::string>(event_data.affected);
                for (const auto& a : sub.filter_affected) {
                    if (affected_str == a) {
                        match = true;
                        break;
                    }
                }
            }

            // Check tags filter
            if (match && !sub.filter_tags.empty()) {
                match = false;
                for (const auto& tag : sub.filter_tags) {
                    for (const auto& et : event_data.tags) {
                        if (et == tag) {
                            match = true;
                            break;
                        }
                    }
                    if (match) break;
                }
            }

            if (match && onSubEvent_) {
                onSubEvent_(client_id, event_data);
                sub.count++;
            }
        }
    } catch (const std::exception& e) {
        MLOG_E("Failed to process on_event: {}", e.what());
        throw;
    }
}

} // namespace lunaricorn
