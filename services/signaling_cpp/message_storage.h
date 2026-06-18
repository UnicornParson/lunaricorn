#ifndef MESSAGE_STORAGE_H
#define MESSAGE_STORAGE_H

#include <Poco/Logger.h>
#include <Poco/SharedPtr.h>
#include <vector>
#include <string>
#include <memory>

// Forward declarations for data types
class EventData;
class EventDataExtended;
struct DbConfig;

class StorageError : public std::exception
{
public:
    StorageError(const std::string& message) : _message(message) {}
    const char* what() const noexcept override { return _message.c_str(); }
private:
    std::string _message;
};

class BrokenStorageError : public std::exception
{
public:
    BrokenStorageError(const std::string& message) : _message(message) {}
    const char* what() const noexcept override { return _message.c_str(); }
private:
    std::string _message;
};

class MessageStorage
{
public:
    MessageStorage(const DbConfig& db_cfg);
    ~MessageStorage();

    int createEvent(const EventData& event_data);
    std::vector<std::string> getUniqueValues(const std::string& field_name);
    std::vector<EventDataExtended> findEvents(double timestamp, 
                                             const std::vector<std::string>& types = {},
                                             const std::vector<std::string>& sources = {},
                                             const std::vector<std::string>& affected = {},
                                             const std::vector<std::string>& tags = {},
                                             int limit = 0);
    std::vector<EventDataExtended> findEventsByType(const std::string& event_type);

private:
    void initializeDatabase();
    std::vector<EventDataExtended> resultToEventDataExtendedList(const std::vector<std::vector<std::string>>& data_list);
    
    DbConfig db_cfg_;
    bool ready_;
    bool db_enabled_;
    Poco::Logger& logger_;
    std::shared_ptr<Poco::NotificationCenter> notification_center_;
};

#endif // MESSAGE_STORAGE_H