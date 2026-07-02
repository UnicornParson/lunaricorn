#include "test_sender.h"
#include <iostream>
#include <stop_token>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <random>
#include <chrono>

#include <boost/json.hpp>

#include "proto/signaling.h"
#include "event_data.h"

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

static const char* event_types[] = {
    "orb.trade",
    "orb.position",
    "orb.portfolio",
    "orb.market_data",
    "orb.maintenance",
    "orb.alert",
    "orb.system"
};

static const char* event_sources[] = {
    "orb",
    "maintenance",
    "monitor",
    "trader",
    "risk_engine"
};

static const char* tags[][3] = {
    {"high_priority", "critical", "urgent"},
    {"News", "Breaking", "Alert"},
    {"trade", "execution", "fill"},
    {"risk", "limit", "breach"},
    {"system", "health", "status"},
    {"portfolio", "rebalance", "adjust"},
    {"market", "open", "close"}
};

static std::mt19937 g_rng(static_cast<unsigned>(
    std::chrono::steady_clock::now().time_since_epoch().count()));

TestSender::TestSender() = default;
TestSender::~TestSender()
{
    stop();
}

bool TestSender::start(const std::string& host, uint16_t port)
{
    if (m_running.load()) {
        std::cerr << "[" << now_ts() << "] [TestSender] Already running" << std::endl;
        return false;
    }

    m_host = host;
    m_port = port;
    m_running.store(true);

    m_connector = std::make_shared<lunaricorn::SignalingConnector>();

    // Set response callback for test messages
    m_connector->set_response_callback([this](const lunaricorn::SignalingResponse& resp) {
        this->on_test_response(resp);
    });

    // Connect
    if (!m_connector->start(host, port)) {
        std::cerr << "[" << now_ts() << "] [TestSender] Failed to connect to "
                  << host << ":" << port << std::endl;
        m_running.store(false);
        return false;
    }

    std::cout << "[" << now_ts() << "] [TestSender] Connected to " << host << ":" << port << std::endl;

    // Start the thread
    m_stop_source = std::stop_source();
    m_thread = std::thread(&TestSender::runner, this, m_stop_source.get_token());

    std::cout << "[" << now_ts() << "] [TestSender] Test sender started" << std::endl;
    return true;
}

void TestSender::stop()
{
    if (!m_running.load()) {
        return;
    }

    m_running.store(false);
    if (m_connector) {
        m_connector->stop();
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    std::cout << "[" << now_ts() << "] [TestSender] Stopped. sent=" << m_sent_count.load()
              << " errors=" << m_error_count.load() << std::endl;
}

bool TestSender::is_running() const
{
    return m_running.load();
}

uint64_t TestSender::get_sent_count() const
{
    return m_sent_count.load();
}

uint64_t TestSender::get_error_count() const
{
    return m_error_count.load();
}

void TestSender::runner(std::stop_token /*stopToken*/)
{
    std::cout << "[" << now_ts() << "] [TestSender] Runner thread started" << std::endl;

    while (m_running.load()) {
        send_test_event();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "[" << now_ts() << "] [TestSender] Runner thread exiting" << std::endl;
}

void TestSender::send_test_event()
{
    if (!m_running.load() || !m_connector || !m_connector->ready()) {
        m_error_count.fetch_add(1);
        return;
    }

    // Pick random event type
    std::uniform_int_distribution<> dist_type(0, static_cast<int>(std::size(event_types) - 1));
    std::uniform_int_distribution<> dist_source(0, static_cast<int>(std::size(event_sources) - 1));
    std::uniform_int_distribution<> dist_tags(0, static_cast<int>(std::size(tags) - 1));
    std::uniform_int_distribution<> dist_value(1000, 9999);

    const int typeIdx = dist_type(g_rng);
    const int srcIdx  = dist_source(g_rng);
    const int tagIdx  = dist_tags(g_rng);

    lunaricorn::internal::SignalingEvent event;
    event.type = event_types[typeIdx];
    event.source = event_sources[srcIdx];
    event.timestamp.timestamp();

    // Add random tags
    for (int i = 0; i < 3; ++i) {
        event.tags.push_back(tags[tagIdx][i]);
    }

    // Build payload
    boost::json::object payload;
    payload["test_id"] = boost::json::value(static_cast<int64_t>(dist_value(g_rng)));
    payload["seq"] = boost::json::value(static_cast<int64_t>(m_sent_count.fetch_add(1) + 1));
    payload["random_value"] = boost::json::value(static_cast<double>(dist_value(g_rng)) / 100.0);
    payload["event_type_idx"] = boost::json::value(typeIdx);
    event.payload = std::move(payload);

    bool ok = m_connector->push(event);
    if (!ok) {
        m_error_count.fetch_add(1);
        std::cerr << "[" << now_ts() << "] [TestSender] Failed to push event" << std::endl;
    } else {
        std::cout << "[" << now_ts() << "] [TestSender] Pushed event seq="
                  << m_sent_count.load()
                  << " type=" << event.type
                  << " source=" << event.source
                  << " payload=" << boost::json::serialize(event.payload)
                  << std::endl;
    }
}

void TestSender::on_test_response(const lunaricorn::SignalingResponse& resp)
{
    std::cout << "[" << now_ts() << "] [TestSender] Response seq=" << resp._seq
              << " ok=" << (resp.ok ? "true" : "false");
    if (!resp.error.empty()) {
        std::cout << " error=" << resp.error;
    }
    std::cout << std::endl;
}