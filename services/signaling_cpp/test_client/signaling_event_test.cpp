#include <boost/test/unit_test.hpp>
#include <boost/json.hpp>

#include <list>
#include <string>

#include <Poco/DateTime.h>
#include <Poco/Timestamp.h>

#include <proto/signaling.h>
#include <lunaricorn.h>


using namespace lunaricorn::internal;

struct SimpleFixture {
    SimpleFixture() {
        // make log stub mode
        lunaricorn::MLog::is_stub = true;
    }
};

BOOST_FIXTURE_TEST_SUITE(SignalingEventSuite, SimpleFixture)

BOOST_AUTO_TEST_CASE(test_from_dict_valid)
{
    boost::json::object payload;
    payload["answer"] = 42;
    payload["name"] = "alice";

    boost::json::array tags;
    tags.emplace_back("alpha");
    tags.emplace_back("beta");

    boost::json::object data;
    data["type"] = "push";
    data["client_id"] = "client_123";
    data["event_type"] = "chat.message";
    data["message"] = payload;
    data["tags"] = tags;
    data["timestamp"] = 1710000000.5;

    SignalingEvent ev;

    BOOST_REQUIRE(ev.fromDict(data));

    BOOST_CHECK_EQUAL(ev.type, "chat.message");
    BOOST_CHECK_EQUAL(ev.source, "client_123");
    BOOST_CHECK_EQUAL(ev.tags.size(), 2u);

    auto it = ev.tags.begin();

    BOOST_REQUIRE(it != ev.tags.end());
    BOOST_CHECK_EQUAL(*it, "alpha");

    ++it;

    BOOST_REQUIRE(it != ev.tags.end());
    BOOST_CHECK_EQUAL(*it, "beta");

    BOOST_CHECK(ev.payload.contains("answer"));
    BOOST_CHECK(ev.payload.contains("name"));

    BOOST_CHECK_EQUAL(ev.payload["answer"].as_int64(), 42);
    BOOST_CHECK_EQUAL(ev.payload["name"].as_string(), "alice");
}

BOOST_AUTO_TEST_CASE(test_from_dict_missing_event_type)
{
    boost::json::object data;
    data["client_id"] = "client_123";
    data["message"] = boost::json::object{};
    data["timestamp"] = 1.0;

    SignalingEvent ev;

    BOOST_CHECK(!ev.fromDict(data));
}

BOOST_AUTO_TEST_CASE(test_from_dict_invalid_event_type)
{
    boost::json::object data;
    data["event_type"] = 123;
    data["client_id"] = "client_123";
    data["message"] = boost::json::object{};
    data["timestamp"] = 1.0;

    SignalingEvent ev;

    BOOST_CHECK(!ev.fromDict(data));
}

BOOST_AUTO_TEST_CASE(test_from_dict_missing_client_id)
{
    boost::json::object data;
    data["event_type"] = "chat.message";
    data["message"] = boost::json::object{};
    data["timestamp"] = 1.0;

    SignalingEvent ev;

    BOOST_CHECK(!ev.fromDict(data));
}

BOOST_AUTO_TEST_CASE(test_from_dict_invalid_message)
{
    boost::json::object data;
    data["event_type"] = "chat.message";
    data["client_id"] = "client_123";
    data["message"] = "not_object";
    data["timestamp"] = 1.0;

    SignalingEvent ev;

    BOOST_CHECK(!ev.fromDict(data));
}

BOOST_AUTO_TEST_CASE(test_from_dict_invalid_timestamp)
{
    boost::json::object data;
    data["event_type"] = "chat.message";
    data["client_id"] = "client_123";
    data["message"] = boost::json::object{};
    data["timestamp"] = "invalid";

    SignalingEvent ev;

    BOOST_CHECK(!ev.fromDict(data));
}

BOOST_AUTO_TEST_CASE(test_from_dict_ignores_invalid_tags)
{
    boost::json::array tags;
    tags.emplace_back("valid");
    tags.emplace_back(123);
    tags.emplace_back(true);
    tags.emplace_back("another");

    boost::json::object data;
    data["event_type"] = "chat.message";
    data["client_id"] = "client_123";
    data["message"] = boost::json::object{};
    data["tags"] = tags;
    data["timestamp"] = 1.0;

    SignalingEvent ev;

    BOOST_REQUIRE(ev.fromDict(data));

    BOOST_CHECK_EQUAL(ev.tags.size(), 2u);

    auto it = ev.tags.begin();

    BOOST_REQUIRE(it != ev.tags.end());
    BOOST_CHECK_EQUAL(*it, "valid");

    ++it;

    BOOST_REQUIRE(it != ev.tags.end());
    BOOST_CHECK_EQUAL(*it, "another");
}

BOOST_AUTO_TEST_CASE(test_to_dict_basic)
{
    SignalingEvent ev;

    ev.type = "room.join";
    ev.source = "client_xyz";

    ev.tags.emplace_back("rtc");
    ev.tags.emplace_back("voice");

    ev.payload["channel"] = "main";
    ev.payload["users"] = 5;

    ev.timestamp = Poco::Timestamp(1710000000123456);

    boost::json::object data = ev.toDict();

    BOOST_REQUIRE(data.contains("type"));
    BOOST_REQUIRE(data.contains("client_id"));
    BOOST_REQUIRE(data.contains("event_type"));
    BOOST_REQUIRE(data.contains("message"));
    BOOST_REQUIRE(data.contains("timestamp"));
    BOOST_REQUIRE(data.contains("tags"));

    BOOST_CHECK_EQUAL(data["type"].as_string(), "push");
    BOOST_CHECK_EQUAL(data["client_id"].as_string(), "client_xyz");
    BOOST_CHECK_EQUAL(data["event_type"].as_string(), "room.join");

    const auto& msg = data["message"].as_object();

    BOOST_CHECK_EQUAL(msg.at("channel").as_string(), "main");
    BOOST_CHECK_EQUAL(msg.at("users").as_int64(), 5);

    const auto& tags = data["tags"].as_array();

    BOOST_REQUIRE_EQUAL(tags.size(), 2u);

    BOOST_CHECK_EQUAL(tags[0].as_string(), "rtc");
    BOOST_CHECK_EQUAL(tags[1].as_string(), "voice");

    BOOST_CHECK(data["timestamp"].is_double());

    const double ts = data["timestamp"].as_double();

    BOOST_CHECK_CLOSE(ts, 1710000000.123456, 0.0001);
}

BOOST_AUTO_TEST_CASE(test_roundtrip)
{
    boost::json::object original;
    original["type"] = "push";
    original["client_id"] = "client_777";
    original["event_type"] = "signal.offer";

    boost::json::object payload;
    payload["sdp"] = "test_sdp";
    payload["index"] = 7;

    original["message"] = payload;

    boost::json::array tags;
    tags.emplace_back("webrtc");
    tags.emplace_back("offer");

    original["tags"] = tags;
    original["timestamp"] = 1711111111.25;

    SignalingEvent ev;

    BOOST_REQUIRE(ev.fromDict(original));

    boost::json::object restored = ev.toDict();

    BOOST_CHECK_EQUAL(restored["type"].as_string(), "push");
    BOOST_CHECK_EQUAL(restored["client_id"].as_string(), "client_777");
    BOOST_CHECK_EQUAL(restored["event_type"].as_string(), "signal.offer");

    const auto& restoredPayload = restored["message"].as_object();

    BOOST_CHECK_EQUAL(restoredPayload.at("sdp").as_string(), "test_sdp");
    BOOST_CHECK_EQUAL(restoredPayload.at("index").as_int64(), 7);

    const auto& restoredTags = restored["tags"].as_array();

    BOOST_REQUIRE_EQUAL(restoredTags.size(), 2u);

    BOOST_CHECK_EQUAL(restoredTags[0].as_string(), "webrtc");
    BOOST_CHECK_EQUAL(restoredTags[1].as_string(), "offer");
}

BOOST_AUTO_TEST_SUITE_END()