#pragma once
#include "stdafx.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "message_storage.h"
#include "maintenance.h"

namespace lunaricorn {

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

private:
    std::unique_ptr<MessageStorage> storage_;
    std::mutex mutex_;
};

} // namespace lunaricorn