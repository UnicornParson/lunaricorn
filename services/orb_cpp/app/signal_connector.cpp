#include "signal_connector.h"

#include <iostream>

SignalConnector::SignalConnector()
    : _signaling(nullptr)
{
}

SignalConnector::~SignalConnector()
{
    stop();
}

bool SignalConnector::initialize(const Config& cfg)
{
    // Stop any existing connector
    stop();

    _signaling = std::make_shared<lunaricorn::SignalingConnector>();
    
    // Start signaling connector
    if (!_signaling->start(cfg.host, cfg.req_port)) {
        MLOG_E("SignalConnector: failed to start signaling connection to {}:{}", cfg.host, cfg.req_port);
        _signaling.reset();
        _ready = false;
        return false;
    }

    // Wait briefly for connection to establish
    int wait_count = 0;
    while (!_signaling->ready() && wait_count < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ++wait_count;
    }

    if (!_signaling->ready()) {
        MLOG_E("SignalConnector: signaling connection timeout after {}ms", wait_count * 100);
        _signaling.reset();
        _ready = false;
        return false;
    }

    _ready = true;
    MLOG_I("SignalConnector: connected to signaling at {}:{}", cfg.host, cfg.req_port);
    return true;
}

bool SignalConnector::ready() const
{
    return _ready && _signaling != nullptr && _signaling->ready();
}

void SignalConnector::stop()
{
    if (_signaling) {
        _signaling->stop();
        _signaling.reset();
    }
    _ready = false;
}

bool SignalConnector::push_event(const std::string& event_type, const std::string& object_id, const std::string& uuid)
{
    if (!ready()) {
        return false;
    }

    // Build the signaling event
    lunaricorn::internal::SignalingEvent event;
    event.type = event_type;
    event.source = "orb_cpp";
    event.timestamp = Poco::DateTime();

    // Set payload
    event.payload["id"] = boost::json::value(object_id);
    event.payload["uuid"] = boost::json::value(uuid);

    // Set tags
    event.tags.push_back("orb");

    // Push the event
    return _signaling->push(event);
}