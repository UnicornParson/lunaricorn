#include <iostream>
#include <string>
#include <thread>
#include <stop_token>
#include <chrono>
#include <cstring>
#include <csignal>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <ctime>

#include <Poco/Net/StreamSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Timer.h>
#include <Poco/Format.h>

#include <boost/json.hpp>

#include "signaling_api.h"
#include "proto/signaling.h"
#include "event_data.h"
#include "test_sender.h"

using namespace lunaricorn;
using namespace lunaricorn::internal;

static std::atomic<bool> g_running{true};
static std::atomic<TestSender*> g_test_sender{nullptr};

static void signal_handler(int signum)
{
    (void)signum;
    g_running = false;
    TestSender* ts = g_test_sender.load();
    if (ts) {
        ts->stop();
    }
}

// --- message type name ---
static const char* msg_type_name(MessageType t)
{
    switch (t) {
        case MT_HB:        return "HB";
        case MT_Response:  return "RESP";
        case MT_PubReq:    return "PUB";
        case MT_QueryReq:  return "QUERY";
        case MT_Sub:       return "SUB";
        default:           return "UNKNOWN";
    }
}

static const char* content_type_name(ContentType ct)
{
    switch (ct) {
        case CT_Raw:  return "RAW";
        case CT_Json: return "JSON";
        default:      return "UNKNOWN";
    }
}

// --- helpers ---

static std::string now_ts()
{
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;
    std::tm tm_buf{};
    gmtime_r(&t, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    oss << "Z";
    return oss.str();
}

static std::string serialize_json(const boost::json::object& obj)
{
    return boost::json::serialize(obj);
}

// --- print helper: one message per line ---
static void print_message(const char* direction, const MessageHeader& header, const boost::json::object& data)
{
    std::ostringstream line;
    line << "[" << now_ts() << "] "
         << direction << " | type=" << msg_type_name(static_cast<MessageType>(header.type))
         << " data_type=" << content_type_name(static_cast<ContentType>(header.data_type))
         << " seq=" << header.seq
         << " data_len=" << header.data_len;

    if (!data.empty()) {
        line << " | " << serialize_json(data);
    }
    std::cout << line.str() << std::endl;
}

// --- subscription callback ---
// Called when server sends subscription events (pushed events to subscribed client)
static void on_subscription(const SignalingSubEvent& sub)
{
    for (const auto& ev : sub.events) {
        // Build event info
        boost::json::object ev_info;
        ev_info["type"] = boost::json::value(ev.type);
        ev_info["source"] = boost::json::value(ev.source);
        
        // Convert timestamp
        auto ts = ev.timestamp.timestamp();
        ev_info["timestamp"] = boost::json::value(static_cast<int64_t>(ts.epochMicroseconds()) / 1000000);
        
        boost::json::array tags_arr;
        for (const auto& tag : ev.tags) {
            tags_arr.push_back(boost::json::value(tag));
        }
        ev_info["tags"] = std::move(tags_arr);
        
        // Copy payload
        ev_info["payload"] = ev.payload;
        
        // Print each event on its own line
        MessageHeader hdr{};
        hdr.magic = HeaderMagic;
        hdr.version = PROTOCOL_VERSION;
        hdr.type = MT_Sub;
        hdr.data_type = CT_Json;
        hdr.flags = 0;
        hdr.seq = sub._seq;
        hdr.data_len = 0;
        hdr.crc = 0;
        
        print_message("SUB", hdr, ev_info);
    }
}

// --- response callback ---
// Called when server sends MT_Response messages
static void on_response(const SignalingResponse& resp)
{
    MessageHeader hdr{};
    hdr.magic = HeaderMagic;
    hdr.version = PROTOCOL_VERSION;
    hdr.type = MT_Response;
    hdr.data_type = CT_Json;
    hdr.flags = 0;
    hdr.seq = resp._seq;
    hdr.data_len = 0;
    hdr.crc = 0;

    print_message("RESP", hdr, resp.data);
}

// --- push request callback ---
// Called when server sends MT_PubReq messages (raw event pushes)
static void on_push_event(const SignalingEvent& event)
{
    boost::json::object ev_info;
    ev_info["type"] = boost::json::value(event.type);
    ev_info["source"] = boost::json::value(event.source);
    
    auto ts = event.timestamp.timestamp();
    ev_info["timestamp"] = boost::json::value(static_cast<int64_t>(ts.epochMicroseconds()) / 1000000);
    
    boost::json::array tags_arr;
    for (const auto& tag : event.tags) {
        tags_arr.push_back(boost::json::value(tag));
    }
    ev_info["tags"] = std::move(tags_arr);
    ev_info["payload"] = event.payload;
    
    MessageHeader hdr{};
    hdr.magic = HeaderMagic;
    hdr.version = PROTOCOL_VERSION;
    hdr.type = MT_PubReq;
    hdr.data_type = CT_Json;
    hdr.flags = 0;
    hdr.seq = 0;
    hdr.data_len = 0;
    hdr.crc = 0;
    
    print_message("PUSH", hdr, ev_info);
}

// --- disconnect callback ---
static void on_disconnect(const std::string& reason, uint64_t /*magic*/)
{
    std::cout << "[" << now_ts() << "] DISCONNECT: " << reason << std::endl;
}

int main(int argc, char* argv[])
{
    // Install signal handlers
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);
    lunaricorn::MLog::is_stub = true;
    lunaricorn::MLog::owner = "signaling_cli";
    lunaricorn::MLog::token = "c9e29f33-f893-49ef-81ac-f921c69372be";

    std::string host = "127.0.0.1";
    uint16_t port  = 8080;

    if (argc > 1) host = argv[1];
    if (argc > 2) port = static_cast<uint16_t>(std::stoi(argv[2]));

    std::cout << "=== Signaling CLI ===" << std::endl;
    std::cout << "Connecting to " << host << ":" << port << std::endl;
    std::cout << "Press Ctrl+C to exit" << std::endl;
    std::cout << "=====================" << std::endl;

    // Main connector for receiving messages
    SignalingConnector connector;

    // Set up callbacks
    connector.set_response_callback(on_response);
    connector.set_subscription_callback(on_subscription);
    connector.set_disconnect_callback(on_disconnect);

    // Connect
    if (!connector.start(host, port)) {
        std::cerr << "Failed to connect to " << host << ":" << port << std::endl;
        return 1;
    }

    std::cout << "[" << now_ts() << "] Connected!" << std::endl;

    // Subscribe to ALL event types (empty filter = subscribe to everything)
    if (!connector.subscribe({})) {
        std::cerr << "Failed to send subscription!" << std::endl;
        connector.stop();
        return 1;
    }
    std::cout << "[" << now_ts() << "] Subscription sent (all event types)" << std::endl;

    // Start test sender in separate thread
    TestSender test_sender;
    g_test_sender.store(&test_sender);

    if (test_sender.start(host, port)) {
        std::cout << "[" << now_ts() << "] Test sender started" << std::endl;
    } else {
        std::cerr << "[" << now_ts() << "] Failed to start test sender" << std::endl;
    }

    std::cout << std::endl;

    // Wait for disconnect or Ctrl+C
    while (g_running && connector.ready()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << std::endl << "[" << now_ts() << "] Exiting..." << std::endl;
    connector.stop();
    return 0;
}
