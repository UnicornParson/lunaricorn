#pragma once
#include "stdafx.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
#include "message_storage.h"
#include "maintenance.h"
#include <functional>

namespace lunaricorn {
using tp_t = std::chrono::time_point<std::chrono::system_clock>;
struct Subscriber
{
    tp_t reg_point = std::chrono::system_clock::now();
    std::vector<std::string> filter_types;
    std::vector<std::string> filter_sources;
    std::vector<std::string> filter_affected;
    std::vector<std::string> filter_tags;
    uint64_t client_id;
    uint64_t count;
};

class SignalingEngine
{
public:
    explicit SignalingEngine(const DbConfig& db_cfg);
    ~SignalingEngine();

    long long createEvent(const StoredEventData& event_data);
    std::vector<std::string> getUniqueValues(const std::string& field_name);
    std::vector<StoredEventDataExtended> findEvents(double timestamp, 
                                                    const std::vector<std::string>& types = {},
                                                    const std::vector<std::string>& sources = {},
                                                    const std::vector<std::string>& affected = {},
                                                    const std::vector<std::string>& tags = {},
                                                    int limit = 0);
    std::vector<StoredEventDataExtended> findEventsByType(const std::string& event_type);
    void subscribe(uint64_t client_id,
                    const std::vector<std::string>& types = {},
                    const std::vector<std::string>& sources = {},
                    const std::vector<std::string>& affected = {},
                    const std::vector<std::string>& tags = {});
    void unsubscribe(uint64_t client_id);

    // Set the callback for subscribed events
    // Called for each subscriber whose filters match the event
    void setOnSubEvent(std::function<void(uint64_t, const StoredEventData&)> cb);

    // Dispatch an event to all matching subscribers (public API wrapper for on_event)
    void dispatchEvent(const StoredEventData& event_data);

private:
    void on_event(const StoredEventData& event_data);
    std::unique_ptr<MessageStorage> storage_;
    std::mutex mutex_;
    std::map<uint64_t, Subscriber> subscribers_;
    std::function<void(uint64_t, const StoredEventData&)> onSubEvent_;
};

} // namespace lunaricorn