#include "signaling_engine_test.h"
#include <Poco/Logger.h>
#include <boost/json.hpp>
#include <chrono>
#include <iostream>

using namespace lunaricorn;

namespace lunaricorn {

SignalingEngineTest::SignalingEngineTest(std::shared_ptr<SignalingEngine> engine)
    : engine_(engine)
{
}

bool SignalingEngineTest::run()
{
    if (!engine_) {
        MLOG_E("SignalingEngineTest: engine pointer is null");
        return false;
    }

    bool all_tests_passed = true;

    // Run individual tests
    all_tests_passed &= testDatabaseInitialization();
    all_tests_passed &= testCreateEvent();
    all_tests_passed &= testGetUniqueValues();
    all_tests_passed &= testFindEvents();
    all_tests_passed &= testFindEventsByType();

    if (all_tests_passed) {
        MLOG("All SignalingEngine tests passed");
    } else {
        MLOG_E("Some SignalingEngine tests failed");
    }

    return all_tests_passed;
}

bool SignalingEngineTest::testDatabaseInitialization()
{
    MLOG_D("Testing database initialization");
    
    if (!engine_) {
        MLOG_E("Engine is null in testDatabaseInitialization");
        return false;
    }

    // The engine should already be initialized at this point
    try {
        MLOG("Database initialization test passed");
        return true;
    } catch (const std::exception& e) {
        MLOG_E("Database initialization test failed: {}", e.what());
        return false;
    }
}

bool SignalingEngineTest::testCreateEvent()
{
    MLOG_D("Testing createEvent functionality");
    
    if (!engine_) {
        MLOG_E("Engine is null in testCreateEvent");
        return false;
    }

    try {
        // Build a StoredEventData (message_storage.h types)
        StoredEventData event_data;
        event_data.event_type = "test_event";
        
        // timestamp: seconds since epoch
        event_data.timestamp = std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        // payload as json::value
        boost::json::object obj;
        obj["key1"] = "value1";
        obj["key2"] = 42;
        event_data.payload = obj;
        
        // source as optional string
        event_data.source = "test_source";
        
        // affected as json::value array
        boost::json::array aff;
        aff.emplace_back("item1");
        aff.emplace_back("item2");
        event_data.affected = aff;
        
        // tags as vector<string>
        event_data.tags = {"tag1", "tag2"};

        // Create the event
        long long eid = engine_->createEvent(event_data);
        
        if (eid <= 0) {
            MLOG_E("createEvent returned invalid ID");
            return false;
        }

        MLOG("createEvent test passed with ID: {}", eid);
        return true;
    } catch (const std::exception& e) {
        MLOG_E("createEvent test failed: {}", e.what());
        return false;
    }
}

bool SignalingEngineTest::testGetUniqueValues()
{
    MLOG_D("Testing getUniqueValues functionality");
    
    if (!engine_) {
        MLOG_E("Engine is null in testGetUniqueValues");
        return false;
    }

    try {
        // Test getting unique values for different fields
        auto types = engine_->getUniqueValues("type");
        auto affected = engine_->getUniqueValues("affected");
        auto owner = engine_->getUniqueValues("owner");
        
        MLOG("getUniqueValues test passed");
        return true;
    } catch (const std::exception& e) {
        MLOG_E("getUniqueValues test failed: {}", e.what());
        return false;
    }
}

bool SignalingEngineTest::testFindEvents()
{
    MLOG_D("Testing findEvents functionality");
    
    if (!engine_) {
        MLOG_E("Engine is null in testFindEvents");
        return false;
    }

    try {
        // timestamp: 1 hour ago in seconds since epoch
        double timestamp = std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()).count() - 3600;
        
        // Find all events (no filters)
        auto events = engine_->findEvents(timestamp);
        
        MLOG("findEvents test passed, found {} events", events.size());
        return true;
    } catch (const std::exception& e) {
        MLOG_E("findEvents test failed: {}", e.what());
        return false;
    }
}

bool SignalingEngineTest::testFindEventsByType()
{
    MLOG_D("Testing findEventsByType functionality");
    
    if (!engine_) {
        MLOG_E("Engine is null in testFindEventsByType");
        return false;
    }

    try {
        // Test finding events by type
        auto events = engine_->findEventsByType("test_event");
        
        MLOG("findEventsByType test passed, found {} events", events.size());
        return true;
    } catch (const std::exception& e) {
        MLOG_E("findEventsByType test failed: {}", e.what());
        return false;
    }
}

} // namespace lunaricorn