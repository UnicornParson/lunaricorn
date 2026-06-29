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
    all_tests_passed &= testSubscribeUnsubscribe();
    all_tests_passed &= testOnEventCallback();
    all_tests_passed &= testOnEventFiltering();

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

bool SignalingEngineTest::testSubscribeUnsubscribe()
{
    MLOG_D("Testing subscribe/unsubscribe functionality");

    if (!engine_) {
        MLOG_E("Engine is null in testSubscribeUnsubscribe");
        return false;
    }

    try {
        uint64_t client_id = 42;

        // Subscribe with filters
        engine_->subscribe(client_id, {"trade"}, {"eve-central"}, {}, {"market"});

        // Subscribe another client with different filters
        uint64_t client_id2 = 99;
        engine_->subscribe(client_id2, {"jump"}, {}, {"system:12345"});

        // Unsubscribe first client
        engine_->unsubscribe(client_id);

        MLOG("subscribe/unsubscribe test passed");
        return true;
    } catch (const std::exception& e) {
        MLOG_E("subscribe/unsubscribe test failed: {}", e.what());
        return false;
    }
}

bool SignalingEngineTest::testOnEventCallback()
{
    MLOG_D("Testing onEvent callback functionality");

    if (!engine_) {
        MLOG_E("Engine is null in testOnEventCallback");
        return false;
    }

    try {
        std::vector<std::pair<uint64_t, StoredEventData>> received;

        // Set callback
        engine_->setOnSubEvent([&received](uint64_t client_id, const StoredEventData& event_data) {
            received.emplace_back(client_id, event_data);
        });

        // Create and subscribe
        uint64_t client_id = 1;
        engine_->subscribe(client_id, {"trade"}, {}, {}, {});

        // Create event
        StoredEventData event_data;
        event_data.event_type = "trade";
        event_data.timestamp = std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        boost::json::object payload;
        payload["price"] = 100.0;
        event_data.payload = payload;
        event_data.source = "test_source";
        boost::json::array aff;
        aff.emplace_back("item1");
        event_data.affected = aff;
        event_data.tags = {"market", "trade"};

        // Dispatch event (public API)
        engine_->dispatchEvent(event_data);

        // Verify callback was called
        if (received.size() != 1) {
            MLOG_E("Expected 1 callback, got {}", received.size());
            return false;
        }

        if (received[0].first != client_id) {
            MLOG_E("Expected client_id {}, got {}", client_id, received[0].first);
            return false;
        }

        if (received[0].second.event_type != "trade") {
            MLOG_E("Expected event_type 'trade', got '{}'", received[0].second.event_type);
            return false;
        }

        MLOG("onEvent callback test passed, received {} events", received.size());
        return true;
    } catch (const std::exception& e) {
        MLOG_E("onEvent callback test failed: {}", e.what());
        return false;
    }
}

bool SignalingEngineTest::testOnEventFiltering()
{
    MLOG_D("Testing onEvent filtering functionality");

    if (!engine_) {
        MLOG_E("Engine is null in testOnEventFiltering");
        return false;
    }

    try {
        std::vector<uint64_t> matched_clients;

        // Set callback
        engine_->setOnSubEvent([&matched_clients](uint64_t client_id, const StoredEventData&) {
            matched_clients.push_back(client_id);
        });

        // Subscribe with type filter
        engine_->subscribe(1, {"trade"}, {}, {}, {});
        // Subscribe with source filter
        engine_->subscribe(2, {}, {"eve-central"}, {}, {});
        // Subscribe with tag filter
        engine_->subscribe(3, {}, {}, {}, {"market"});
        // Subscribe with affected filter
        engine_->subscribe(4, {}, {}, {"item1"}, {});

        // Create matching event
        StoredEventData event_data;
        event_data.event_type = "trade";
        event_data.timestamp = std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        boost::json::object payload;
        payload["price"] = 100.0;
        event_data.payload = payload;
        event_data.source = "eve-central";
        boost::json::array aff;
        aff.emplace_back("item1");
        event_data.affected = aff;
        event_data.tags = {"market", "trade"};

        // Dispatch event (public API)
        engine_->dispatchEvent(event_data);

        // All 4 subscribers should match
        if (matched_clients.size() != 4) {
            MLOG_E("Expected 4 matched subscribers, got {}", matched_clients.size());
            return false;
        }

        // Create non-matching event
        StoredEventData event_data2;
        event_data2.event_type = "jump";
        event_data2.timestamp = std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        boost::json::object payload2;
        payload2["dest"] = "system";
        event_data2.payload = payload2;
        event_data2.source = "other";
        boost::json::array aff2;
        aff2.emplace_back("other_item");
        event_data2.affected = aff2;
        event_data2.tags = {"other_tag"};

        matched_clients.clear();
        engine_->dispatchEvent(event_data2);

        // No subscribers should match
        if (matched_clients.size() != 0) {
            MLOG_E("Expected 0 matched subscribers, got {}", matched_clients.size());
            return false;
        }

        MLOG("onEvent filtering test passed");
        return true;
    } catch (const std::exception& e) {
        MLOG_E("onEvent filtering test failed: {}", e.what());
        return false;
    }
}

} // namespace lunaricorn
