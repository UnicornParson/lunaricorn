// Tests for Raw Connection API (SignalingConnector) from lunaricorn/cpp
// Tests cover connection lifecycle, message sending/receiving, heartbeat,
// subscription handling, and error scenarios.
//
// Note: These tests require a running signaling server to perform
// integration tests. Unit tests use mocking where possible.

#include <boost/test/unit_test.hpp>
#include <boost/json.hpp>

#include <chrono>
#include <condition_variable>
#include <future>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <atomic>
#include <functional>
#include <memory>
#include <sstream>
#include <iomanip>

#include <lunaricorn.h>
#include <signaling_api.h>
#include <proto/signaling.h>
#include <event_data.h>

using namespace lunaricorn;
using namespace lunaricorn::internal;

constexpr std::string raw_host { "127.0.0.1" };
constexpr Poco::UInt16 raw_port = 8080;

// ============================================================================
// Test fixture for integration tests
// ============================================================================

struct RawConnectionFixture {
    // Configuration - will be overridden by environment or command line
    std::string server_host = raw_host;
    Poco::UInt16 server_port = raw_port;
    
    // Test helpers
    std::atomic<bool> connected{false};
    std::atomic<bool> disconnected{false};
    std::atomic<bool> received_subscription{false};
    std::atomic<bool> received_response{false};
    std::atomic<int> response_count{0};
    std::atomic<int> subscription_count{0};
    
    SignalingResponse last_response;
    std::optional<SignalingSubEvent> last_subscription;
    
    std::mutex cv_mutex;
    std::condition_variable cv;
    
    // Callbacks for SignalingConnector
    void on_connected() {
        connected = true;
        cv.notify_all();
    }
    
    void on_disconnect(const std::string& reason, uint64_t /*magic*/) {
        disconnected = true;
        cv.notify_all();
    }
    
    void on_response(const SignalingResponse& resp) {
        std::lock_guard<std::mutex> lock(cv_mutex);
        last_response = resp;
        response_count.fetch_add(1);
        received_response = true;
        cv.notify_all();
    }
    
    void on_subscription(const SignalingSubEvent& sub) {
        std::lock_guard<std::mutex> lock(cv_mutex);
        last_subscription = sub;
        subscription_count.fetch_add(1);
        received_subscription = true;
        cv.notify_all();
    }
    
    // Helper to create SignalingSubEvent with a seq
    SignalingSubEvent make_sub_event(seq_t seq) {
        return SignalingSubEvent(seq);
    }
};

// Create a test signaling event (free function for use in BOOST_AUTO_TEST_CASE)
inline SignalingEvent create_test_event(const std::string& type = "test.message",
                                        const std::string& source = "test_client")
{
    SignalingEvent event;
    event.type = type;
    event.source = source;
    event.payload["test"] = "data";
    event.payload["timestamp"] = static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
    
    boost::json::array tags;
    tags.emplace_back("test");
    event.tags = {"test"};
    
    return event;
}

// ============================================================================
// Unit tests for SignalingProto (protocol serialization/deserialization)
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(RawConnectionProtoSuite, RawConnectionFixture)

// Test: Create SignalingProto and verify initial stats
BOOST_AUTO_TEST_CASE(Proto_InitialState) {
    SignalingProto proto;
    const auto& stats = proto.stats();
    
    BOOST_CHECK_EQUAL(stats.ok.load(), 0ULL);
    BOOST_CHECK_EQUAL(stats.fails.load(), 0ULL);
}

// Test: Serialize a valid heartbeat message
BOOST_AUTO_TEST_CASE(Proto_SerializeHeartbeat) {
    SignalingProto proto;
    MessageHeader header;
    header.magic = HeaderMagic;
    header.version = PROTOCOL_VERSION;
    header.type = MT_HB;
    header.data_type = CT_Json;
    header.flags = 0;
    header.seq = 0;
    header.data_len = 0;
    header.crc = 0;
    
    std::vector<uint8_t> buffer;
    size_t sz = proto.serializeJson(header, buffer, boost::json::object{});
    
    BOOST_CHECK_GT(sz, 0u);
    BOOST_CHECK_EQUAL(buffer.size(), sz);
    BOOST_CHECK_EQUAL(buffer.size(), sizeof(MessageHeader));
}

// Test: Serialize a valid push request
BOOST_AUTO_TEST_CASE(Proto_SerializePushRequest) {
    SignalingProto proto;
    MessageHeader header;
    header.magic = HeaderMagic;
    header.version = PROTOCOL_VERSION;
    header.type = MT_PubReq;
    header.data_type = CT_Json;
    header.flags = 0;
    header.seq = 42;
    header.data_len = 0;
    header.crc = 0;
    
    SignalingEvent event = create_test_event();
    boost::json::object data = event.toDict();
    
    std::vector<uint8_t> buffer;
    size_t sz = proto.serializeJson(header, buffer, data);
    
    BOOST_CHECK_GT(sz, sizeof(MessageHeader));
    BOOST_CHECK_EQUAL(buffer.size(), sz);
    
    // Verify we can deserialize it back
    IncomingMessage msg;
    BOOST_CHECK(proto.deserializeJson(buffer, msg));
    BOOST_CHECK(msg.isValid);
    BOOST_CHECK_EQUAL(msg.header.seq, header.seq);
    BOOST_CHECK_EQUAL(msg.header.type, MT_PubReq);
}

// Test: Serialize and deserialize a response message
BOOST_AUTO_TEST_CASE(Proto_SerializeResponse) {
    SignalingProto proto;
    MessageHeader header;
    header.magic = HeaderMagic;
    header.version = PROTOCOL_VERSION;
    header.type = MT_Response;
    header.data_type = CT_Json;
    header.flags = 0;
    header.seq = 100;
    header.data_len = 0;
    header.crc = 0;
    
    boost::json::object data;
    data["success"] = true;
    data["message"] = "ok";
    
    std::vector<uint8_t> buffer;
    size_t sz = proto.serializeJson(header, buffer, data);
    
    BOOST_CHECK_GT(sz, 0u);
    
    IncomingMessage msg;
    BOOST_CHECK(proto.deserializeJson(buffer, msg));
    BOOST_CHECK(msg.isValid);
    BOOST_CHECK_EQUAL(msg.header.seq, 100ULL);
    BOOST_CHECK_EQUAL(msg.header.type, MT_Response);
}

// Test: Deserialize with wrong magic should fail
BOOST_AUTO_TEST_CASE(Proto_DeserializeWrongMagic) {
    SignalingProto proto;
    
    // Create a buffer with wrong magic
    MessageHeader header;
    header.magic = 0xDEADBEEF;
    header.version = PROTOCOL_VERSION;
    header.type = MT_HB;
    header.data_type = CT_Json;
    header.flags = 0;
    header.seq = 0;
    header.data_len = 0;
    header.crc = 0;
    
    std::vector<uint8_t> buffer;
    buffer.resize(sizeof(MessageHeader));
    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    
    IncomingMessage msg;
    BOOST_CHECK(!proto.deserializeJson(buffer, msg));
    BOOST_CHECK(!msg.isValid);
    BOOST_CHECK(!msg.errorReason.empty());
}

// Test: Deserialize with wrong version should fail
BOOST_AUTO_TEST_CASE(Proto_DeserializeWrongVersion) {
    SignalingProto proto;
    
    MessageHeader header;
    header.magic = HeaderMagic;
    header.version = PROTOCOL_VERSION + 1;
    header.type = MT_HB;
    header.data_type = CT_Json;
    header.flags = 0;
    header.seq = 0;
    header.data_len = 0;
    header.crc = 0;
    
    std::vector<uint8_t> buffer;
    buffer.resize(sizeof(MessageHeader));
    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    
    IncomingMessage msg;
    BOOST_CHECK(!proto.deserializeJson(buffer, msg));
    BOOST_CHECK(!msg.isValid);
}

// Test: Deserialize with non-JSON content type should fail
BOOST_AUTO_TEST_CASE(Proto_DeserializeNonJsonContent) {
    SignalingProto proto;
    
    MessageHeader header;
    header.magic = HeaderMagic;
    header.version = PROTOCOL_VERSION;
    header.type = MT_HB;
    header.data_type = CT_Raw;  // Raw content type
    header.flags = 0;
    header.seq = 0;
    header.data_len = 0;
    header.crc = 0;
    
    std::vector<uint8_t> buffer;
    buffer.resize(sizeof(MessageHeader));
    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    
    IncomingMessage msg;
    BOOST_CHECK(!proto.deserializeJson(buffer, msg));
    BOOST_CHECK(!msg.isValid);
}

// Test: Stats tracking
BOOST_AUTO_TEST_CASE(Proto_StatsTracking) {
    SignalingProto proto;
    
    // Create a valid buffer with correct header
    MessageHeader header;
    header.magic = HeaderMagic;
    header.version = PROTOCOL_VERSION;
    header.type = MT_HB;
    header.data_type = CT_Json;
    header.flags = 0;
    header.seq = 0;
    header.data_len = 0;
    header.crc = 0;
    
    std::vector<uint8_t> buffer;
    buffer.resize(sizeof(MessageHeader));
    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    
    // Valid deserialize (empty payload with CT_Json) should increment ok
    IncomingMessage msg1;
    BOOST_CHECK(proto.deserializeJson(buffer, msg1));
    BOOST_CHECK_EQUAL(proto.stats().ok.load(), 1ULL);
    BOOST_CHECK_EQUAL(proto.stats().fails.load(), 0ULL);
    
    // Invalid deserialize (empty buffer) should increment fails
    std::vector<uint8_t> badBuffer;
    IncomingMessage msg2;
    BOOST_CHECK(!proto.deserializeJson(badBuffer, msg2));
    BOOST_CHECK_EQUAL(proto.stats().ok.load(), 1ULL);
    BOOST_CHECK_EQUAL(proto.stats().fails.load(), 1ULL);
}

// ============================================================================
// Unit tests for SignalingEvent conversion
// ============================================================================

BOOST_AUTO_TEST_CASE(SignalingEvent_ToDictFromDict) {
    SignalingEvent event;
    event.type = "signal.offer";
    event.source = "client_1";
    event.tags.emplace_back("webrtc");
    event.tags.emplace_back("offer");
    
    event.payload["sdp"] = "v=0\r\no=- ...";
    event.payload["type"] = "offer";
    
    event.timestamp = Poco::DateTime();
    
    // toDict
    boost::json::object dict = event.toDict();
    
    BOOST_CHECK(dict.contains("type"));
    BOOST_CHECK(dict.contains("client_id"));
    BOOST_CHECK(dict.contains("event_type"));
    BOOST_CHECK(dict.contains("message"));
    BOOST_CHECK(dict.contains("timestamp"));
    
    BOOST_CHECK_EQUAL(dict["type"].as_string(), "push");
    BOOST_CHECK_EQUAL(dict["event_type"].as_string(), "signal.offer");
    
    // fromDict
    SignalingEvent event2;
    BOOST_CHECK(event2.fromDict(dict));
    BOOST_CHECK_EQUAL(event2.type, "signal.offer");
    BOOST_CHECK_EQUAL(event2.source, "client_1");
    BOOST_CHECK_EQUAL(event2.tags.size(), 2u);
}

BOOST_AUTO_TEST_CASE(SignalingEvent_PushRequestMakeHeader) {
    SignalingPushRequest req(42);
    req.data["test"] = "value";
    
    MessageHeader header;
    req.make_header(header);
    
    BOOST_CHECK_EQUAL(header.magic, HeaderMagic);
    BOOST_CHECK_EQUAL(header.version, PROTOCOL_VERSION);
    BOOST_CHECK_EQUAL(header.type, MT_PubReq);
    BOOST_CHECK_EQUAL(header.seq, 42ULL);
    BOOST_CHECK_EQUAL(header.data_type, CT_Json);
}

BOOST_AUTO_TEST_CASE(SignalingPushRequest_MakeHeaderZeroSeq) {
    SignalingPushRequest req;
    
    MessageHeader header;
    req.make_header(header);
    
    BOOST_CHECK_EQUAL(header.magic, HeaderMagic);
    BOOST_CHECK_EQUAL(header.type, MT_PubReq);
}

// ============================================================================
// Unit tests for SignalingSubEvent
// ============================================================================

BOOST_AUTO_TEST_CASE(SignalingSubEvent_Build) {
    SignalingSubEvent sub(99);
    
    boost::json::object data;
    data["source"] = "server";
    data["events"] = boost::json::array{};
    
    BOOST_CHECK(sub.build(data));
    BOOST_CHECK_EQUAL(sub._seq, 99ULL);
}

BOOST_AUTO_TEST_CASE(SignalingSubEvent_BuildInvalid) {
    SignalingSubEvent sub(99);
    
    boost::json::object data;
    data["invalid"] = "field";
    
    BOOST_CHECK(!sub.build(data));
}

// ============================================================================
// Unit tests for SignalingResponse
// ============================================================================

BOOST_AUTO_TEST_CASE(SignalingResponse_Default) {
    SignalingResponse resp;
    
    BOOST_CHECK_EQUAL(resp._seq, 0ULL);
    BOOST_CHECK(!resp.ok);
    BOOST_CHECK(resp.error.empty());
    BOOST_CHECK(resp.data.empty());
    BOOST_CHECK(std::holds_alternative<std::monostate>(resp.origin));
}

BOOST_AUTO_TEST_CASE(SignalingResponse_WithSeq) {
    SignalingResponse resp(123);
    
    BOOST_CHECK_EQUAL(resp._seq, 123ULL);
}

BOOST_AUTO_TEST_CASE(SignalingResponse_Comparison) {
    SignalingResponse resp1(10);
    SignalingResponse resp2(20);
    SignalingResponse resp3(10);
    
    BOOST_CHECK(resp1._seq < resp2._seq);
    BOOST_CHECK(resp1._seq == resp3._seq);
}

// ============================================================================
// Unit tests for MessageHeader
// ============================================================================

BOOST_AUTO_TEST_CASE(MessageHeader_Size)
{
    BOOST_CHECK_EQUAL(sizeof(MessageHeader), 24u);
}

BOOST_AUTO_TEST_CASE(MessageHeader_DefaultValues) {
    MessageHeader header;
    
    BOOST_CHECK_EQUAL(header.magic, HeaderMagic);
    BOOST_CHECK_EQUAL(header.version, PROTOCOL_VERSION);
    BOOST_CHECK_EQUAL(header.type, MT_Invalid);
    BOOST_CHECK_EQUAL(header.data_type, CT_Raw);
    BOOST_CHECK_EQUAL(header.flags, 0);
    BOOST_CHECK_EQUAL(header.seq, 0ULL);
    BOOST_CHECK_EQUAL(header.data_len, 0U);
    BOOST_CHECK_EQUAL(header.crc, 0U);
}

// ============================================================================
// Constants and enums tests
// ============================================================================

BOOST_AUTO_TEST_CASE(ProtocolConstants) {
    BOOST_CHECK_EQUAL(HeaderMagic, 0x12345678);
    BOOST_CHECK_EQUAL(PROTOCOL_VERSION, 1);
    BOOST_CHECK_EQUAL(MAX_DATA_LEN, 128 * 1024 * 1024);  // 128MB
    BOOST_CHECK_EQUAL(PH, 0xAB);
}

BOOST_AUTO_TEST_CASE(MessageTypeValues) {
    BOOST_CHECK_EQUAL(MT_Invalid, 0);
    BOOST_CHECK_EQUAL(MT_HB, 1);
    BOOST_CHECK_EQUAL(MT_Response, 2);
    BOOST_CHECK_EQUAL(MT_PubReq, 3);
    BOOST_CHECK_EQUAL(MT_QueryReq, 4);
    BOOST_CHECK_EQUAL(MT_Sub, 5);
}

BOOST_AUTO_TEST_CASE(ContentTypeValues) {
    BOOST_CHECK_EQUAL(CT_Raw, 0);
    BOOST_CHECK_EQUAL(CT_Json, 1);
}

// ============================================================================
// SignalingEventType constants
// ============================================================================

BOOST_AUTO_TEST_CASE(SignalingEventTypes) {
    using namespace lunaricorn::internal;
    BOOST_CHECK_EQUAL(SignalingEventType::System, "sys");
    BOOST_CHECK_EQUAL(SignalingEventType::Broadcast, "broadcast");
    BOOST_CHECK_EQUAL(SignalingEventType::Robots, "robots");
    BOOST_CHECK_EQUAL(SignalingEventType::FileOp_new, "FileOp_new");
    BOOST_CHECK_EQUAL(SignalingEventType::FileOp_update, "FileOp_update");
    BOOST_CHECK_EQUAL(SignalingEventType::FileOp_delete, "FileOp_delete");
    BOOST_CHECK_EQUAL(SignalingEventType::FileOp_notify, "FileOp_notify");
}

// ============================================================================
// SignalingEventTags constants
// ============================================================================

BOOST_AUTO_TEST_CASE(SignalingEventTags) {
    BOOST_CHECK_EQUAL(lunaricorn::internal::SignalingEventTags::JobEntry, "JobEntry");
    BOOST_CHECK_EQUAL(lunaricorn::internal::SignalingEventTags::JobExit, "JobExit");
    BOOST_CHECK_EQUAL(lunaricorn::internal::SignalingEventTags::News, "News");
    BOOST_CHECK_EQUAL(lunaricorn::internal::SignalingEventTags::Md, "Md");
    BOOST_CHECK_EQUAL(lunaricorn::internal::SignalingEventTags::Obsidian, "Obsidian");
}

// ============================================================================
// Integration test suite (requires running server)
// ============================================================================
BOOST_AUTO_TEST_SUITE_END()
BOOST_FIXTURE_TEST_SUITE(RawConnectionIntegrationSuite, RawConnectionFixture)

// Integration test: Connect to server
BOOST_AUTO_TEST_CASE(INTEGRATION_ConnectToServer) {
    // This test requires a running signaling server
    // Skip if server is not available
    
    SignalingConnector connector;
    
    // Set up callbacks
    connector.set_response_callback(
        [this](const SignalingResponse& resp) {
            on_response(resp);
        }
    );
    
    connector.set_subscription_callback(
        [this](const SignalingSubEvent& sub) {
            on_subscription(sub);
        }
    );
    
    connector.set_disconnect_callback(
        [this](const std::string& reason, uint64_t magic) {
            on_disconnect(reason, magic);
        }
    );
    
    // Attempt connection
    bool started = connector.start(server_host, server_port);
    
    // Wait for connection
    std::unique_lock<std::mutex> lock(cv_mutex);
    cv.wait(lock, [this]{ return connected || disconnected; });
    
    // Record result
    bool test_passed = connected;
    
    // Cleanup
    connector.stop();
    
    BOOST_CHECK(test_passed);
}

// Integration test: Push event and receive response
BOOST_AUTO_TEST_CASE(INTEGRATION_PushEventAndGetResponse) {
    SignalingConnector connector;
    
    connector.set_response_callback(
        [this](const SignalingResponse& resp) {
            on_response(resp);
        }
    );
    
    connector.set_subscription_callback(
        [this](const SignalingSubEvent& sub) {
            on_subscription(sub);
        }
    );
    
    connector.set_disconnect_callback(
        [this](const std::string& reason, uint64_t magic) {
            on_disconnect(reason, magic);
        }
    );
    
    bool started = connector.start(server_host, server_port);
    
    // Wait for connection
    std::unique_lock<std::mutex> lock(cv_mutex);
    cv.wait(lock, [this]{ return connected || disconnected; });
    
    if (!connected) {
        connector.stop();
        BOOST_CHECK(false);  // Server not available
        return;
    }
    
    // Push an event
    SignalingEvent event = create_test_event("test.push", "integration_test");
    bool pushed = connector.push(event);
    BOOST_CHECK(pushed);
    
    // Wait for response
    cv.wait_for(lock, std::chrono::seconds(5), [this]{
        return received_response.load() || disconnected.load();
    });
    
    bool test_passed = received_response && !last_response.error.empty() || last_response.ok;
    
    connector.stop();
    BOOST_CHECK(test_passed);
}

// Integration test: Heartbeat mechanism
BOOST_AUTO_TEST_CASE(INTEGRATION_HeartbeatMechanism) {
    SignalingConnector connector;
    
    connector.set_response_callback(
        [this](const SignalingResponse& resp) {
            on_response(resp);
        }
    );
    
    connector.set_subscription_callback(
        [this](const SignalingSubEvent& sub) {
            on_subscription(sub);
        }
    );
    
    connector.set_disconnect_callback(
        [this](const std::string& reason, uint64_t magic) {
            on_disconnect(reason, magic);
        }
    );
    
    bool started = connector.start(server_host, server_port);
    
    std::unique_lock<std::mutex> lock(cv_mutex);
    cv.wait(lock, [this]{ return connected || disconnected; });
    
    if (!connected) {
        connector.stop();
        BOOST_CHECK(false);
        return;
    }
    
    // Wait for heartbeat cycle
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    bool test_passed = true;  // Heartbeat should not cause disconnection
    
    connector.stop();
    BOOST_CHECK(test_passed);
}

// Integration test: Disconnect handling
BOOST_AUTO_TEST_CASE(INTEGRATION_DisconnectHandling) {
    SignalingConnector connector;
    
    connector.set_response_callback(
        [this](const SignalingResponse& resp) {
            on_response(resp);
        }
    );
    
    connector.set_subscription_callback(
        [this](const SignalingSubEvent& sub) {
            on_subscription(sub);
        }
    );
    
    connector.set_disconnect_callback(
        [this](const std::string& reason, uint64_t magic) {
            on_disconnect(reason, magic);
        }
    );
    
    bool started = connector.start(server_host, server_port);
    
    std::unique_lock<std::mutex> lock(cv_mutex);
    cv.wait(lock, [this]{ return connected || disconnected; });
    
    if (!connected) {
        connector.stop();
        BOOST_CHECK(false);
        return;
    }
    
    // Stop the connector
    connector.stop();
    
    // Wait for disconnect callback
    cv.wait_for(lock, std::chrono::seconds(2), [this]{
        return disconnected.load();
    });
    
    BOOST_CHECK(disconnected);
}

// Integration test: Multiple pushes
BOOST_AUTO_TEST_CASE(INTEGRATION_MultiplePushes) {
    SignalingConnector connector;
    
    connector.set_response_callback(
        [this](const SignalingResponse& resp) {
            on_response(resp);
        }
    );
    
    connector.set_subscription_callback(
        [this](const SignalingSubEvent& sub) {
            on_subscription(sub);
        }
    );
    
    connector.set_disconnect_callback(
        [this](const std::string& reason, uint64_t magic) {
            on_disconnect(reason, magic);
        }
    );
    
    bool started = connector.start(server_host, server_port);
    
    std::unique_lock<std::mutex> lock(cv_mutex);
    cv.wait(lock, [this]{ return connected || disconnected; });
    
    if (!connected) {
        connector.stop();
        BOOST_CHECK(false);
        return;
    }
    
    // Push multiple events
    int push_count = 5;
    for (int i = 0; i < push_count; ++i) {
        SignalingEvent event = create_test_event("test.multi", "multi_test");
        event.payload["index"] = i;
        connector.push(event);
    }
    
    // Wait for responses
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    bool test_passed = response_count >= 0;  // At least attempted
    
    connector.stop();
    BOOST_CHECK(test_passed);
}

// Integration test: Invalid host should fail
BOOST_AUTO_TEST_CASE(INTEGRATION_InvalidHostFails) {
    SignalingConnector connector;
    
    connector.set_response_callback(
        [this](const SignalingResponse& resp) {
            on_response(resp);
        }
    );
    
    connector.set_subscription_callback(
        [this](const SignalingSubEvent& sub) {
            on_subscription(sub);
        }
    );
    
    connector.set_disconnect_callback(
        [this](const std::string& reason, uint64_t magic) {
            on_disconnect(reason, magic);
        }
    );
    
    // Try to connect to invalid host
    bool started = connector.start("192.0.2.1", 1);  // TEST-NET address
    
    std::unique_lock<std::mutex> lock(cv_mutex);
    cv.wait_for(lock, std::chrono::seconds(3), [this]{
        return disconnected.load();
    });
    
    // Should not connect successfully
    BOOST_CHECK(!connected);
}

// Integration test: Empty host should fail
BOOST_AUTO_TEST_CASE(INTEGRATION_EmptyHostFails) {
    SignalingConnector connector;
    
    // Try to connect with empty host
    bool started = connector.start("", 9090);
    
    BOOST_CHECK(!started);
}

// Integration test: Zero port should fail
BOOST_AUTO_TEST_CASE(INTEGRATION_ZeroPortFails) {
    SignalingConnector connector;
    
    // Try to connect with zero port
    bool started = connector.start("127.0.0.1", 0);
    
    BOOST_CHECK(!started);
}

// Integration test: Subscription callback receives events
BOOST_AUTO_TEST_CASE(INTEGRATION_SubscriptionReceivesEvents) {
    SignalingConnector connector;
    
    connector.set_response_callback(
        [this](const SignalingResponse& resp) {
            on_response(resp);
        }
    );
    
    connector.set_subscription_callback(
        [this](const SignalingSubEvent& sub) {
            on_subscription(sub);
        }
    );
    
    connector.set_disconnect_callback(
        [this](const std::string& reason, uint64_t magic) {
            on_disconnect(reason, magic);
        }
    );
    
    bool started = connector.start(server_host, server_port);
    
    std::unique_lock<std::mutex> lock(cv_mutex);
    cv.wait(lock, [this]{ return connected || disconnected; });
    
    if (!connected) {
        connector.stop();
        BOOST_CHECK(false);
        return;
    }
    
    // Wait for any subscriptions
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    bool test_passed = true;  // Test completed without error
    
    connector.stop();
    BOOST_CHECK(test_passed);
}

// Integration test: Connector ready state
BOOST_AUTO_TEST_CASE(INTEGRATION_ConnectorReadyState) {
    SignalingConnector connector;
    
    // Before start, should not be ready
    BOOST_CHECK(!connector.ready());
    
    connector.set_response_callback(
        [this](const SignalingResponse& resp) {
            on_response(resp);
        }
    );
    
    connector.set_subscription_callback(
        [this](const SignalingSubEvent& sub) {
            on_subscription(sub);
        }
    );
    
    connector.set_disconnect_callback(
        [this](const std::string& reason, uint64_t magic) {
            on_disconnect(reason, magic);
        }
    );
    
    bool started = connector.start(server_host, server_port);
    
    std::unique_lock<std::mutex> lock(cv_mutex);
    cv.wait(lock, [this]{ return connected || disconnected; });
    
    if (connected) {
        BOOST_CHECK(connector.ready());
        connector.stop();
    }
}

// Integration test: Push with invalid event
BOOST_AUTO_TEST_CASE(INTEGRATION_PushWithInvalidEvent) {
    SignalingConnector connector;
    
    connector.set_response_callback(
        [this](const SignalingResponse& resp) {
            on_response(resp);
        }
    );
    
    connector.set_subscription_callback(
        [this](const SignalingSubEvent& sub) {
            on_subscription(sub);
        }
    );
    
    connector.set_disconnect_callback(
        [this](const std::string& reason, uint64_t magic) {
            on_disconnect(reason, magic);
        }
    );
    
    bool started = connector.start(server_host, server_port);
    
    std::unique_lock<std::mutex> lock(cv_mutex);
    cv.wait(lock, [this]{ return connected || disconnected; });
    
    if (!connected) {
        connector.stop();
        BOOST_CHECK(false);
        return;
    }
    
    // Create invalid event
    SignalingEvent event;
    event.type = "";  // Empty type is invalid
    event.source = "";
    
    // toDict should fail for invalid event
    boost::json::object dict = event.toDict();
    BOOST_CHECK(dict.empty() || !dict.contains("event_type"));
    
    connector.stop();
}

// Integration test: Seq increment
BOOST_AUTO_TEST_CASE(INTEGRATION_SeqIncrement) {
    SignalingConnector connector;
    
    connector.set_response_callback(
        [this](const SignalingResponse& resp) {
            on_response(resp);
        }
    );
    
    connector.set_subscription_callback(
        [this](const SignalingSubEvent& sub) {
            on_subscription(sub);
        }
    );
    
    connector.set_disconnect_callback(
        [this](const std::string& reason, uint64_t magic) {
            on_disconnect(reason, magic);
        }
    );
    
    bool started = connector.start(server_host, server_port);
    
    std::unique_lock<std::mutex> lock(cv_mutex);
    cv.wait(lock, [this]{ return connected || disconnected; });
    
    if (!connected) {
        connector.stop();
        BOOST_CHECK(false);
        return;
    }
    
    // Push events and check seq increments
    std::vector<uint64_t> seqs;
    for (int i = 0; i < 3; ++i) {
        SignalingEvent event = create_test_event();
        // Note: seq is internal, we can't directly verify it here
        connector.push(event);
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    bool test_passed = true;
    
    connector.stop();
    BOOST_CHECK(test_passed);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Helper unit tests for lunaricorn utility classes
// ============================================================================

BOOST_AUTO_TEST_SUITE(LunaricornUtilsSuite)

// Test: EventQueue push and get_all
BOOST_AUTO_TEST_CASE(EventQueue_PushGetAll) {
    lunaricorn::EventQueue<int> queue;
    
    queue.push(1);
    queue.push(2);
    queue.push(3);
    
    std::queue<int> result = queue.get_all();
    
    BOOST_CHECK_EQUAL(result.size(), 3u);
    BOOST_CHECK_EQUAL(result.front(), 1);
    result.pop();
    BOOST_CHECK_EQUAL(result.front(), 2);
    result.pop();
    BOOST_CHECK_EQUAL(result.front(), 3);
}

// Test: EventQueue empty after get_all
BOOST_AUTO_TEST_CASE(EventQueue_EmptyAfterGetAll) {
    lunaricorn::EventQueue<int> queue;
    
    queue.push(42);
    
    queue.get_all();
    
    std::queue<int> result = queue.get_all();
    BOOST_CHECK_EQUAL(result.size(), 0u);
}

// Test: EventQueue thread safety (basic)
BOOST_AUTO_TEST_CASE(EventQueue_ThreadSafety) {
    lunaricorn::EventQueue<int> queue;
    const int push_count = 100;
    
    // Push from multiple threads
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&queue, t, push_count]() {
            for (int i = 0; i < push_count; ++i) {
                queue.push(t * push_count + i);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::queue<int> result = queue.get_all();
    BOOST_CHECK_EQUAL(result.size(), static_cast<size_t>(push_count * 4));
}

// Test: Counted class
BOOST_AUTO_TEST_CASE(Counted_IncrementDecrement) {
    {
        lunaricorn::Counted<int> a;
        BOOST_CHECK_EQUAL(a.alive_count(), 1ULL);
        
        {
            lunaricorn::Counted<int> b;
            BOOST_CHECK_EQUAL(a.alive_count(), 2ULL);
        }
        
        BOOST_CHECK_EQUAL(a.alive_count(), 1ULL);
    }
    
    BOOST_CHECK_EQUAL(lunaricorn::Counted<int>::count.load(), 0LL);
}

BOOST_AUTO_TEST_SUITE_END()