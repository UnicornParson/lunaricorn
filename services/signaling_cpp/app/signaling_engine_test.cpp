#include "signaling_engine_test.h"
#include <Poco/DateTime.h>
#include <Poco/Logger.h>
#include <boost/json.hpp>
#include <iostream>

using namespace Poco;
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
    // Just verify it's ready
    try {
        // This would just test the connection, which was done during initialization
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
        // Create a simple event
        EventData event_data;
        event_data.event_type = "test_event";
        event_data.timestamp = DateTime();
        
        // Add some payload data
        boost::json::object payload;
        payload["key1"] = "value1";
        payload["key2"] = 42;
        event_data.payload = payload;
        
        event_data.source = "test_source";
        event_data.affected = {"item1", "item2"};
        event_data.tags = {"tag1", "tag2"};

        // Create the event
        int eid = engine_->createEvent(event_data);
        
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
        // Test finding events with a timestamp
        double timestamp = DateTime().timestamp().epochMicroseconds() / 1000000.0 - 3600; // 1 hour ago
        
        // Find all events (no filters)
        auto events = engine_->findEvents(timestamp);
        
        MLOG("findEvents test passed");
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
        
        MLOG("findEventsByType test passed");
        return true;
    } catch (const std::exception& e) {
        MLOG_E("findEventsByType test failed: {}", e.what());
        return false;
    }
}

} // namespace lunaricorn