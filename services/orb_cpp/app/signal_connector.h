#pragma once

#include <string>
#include <memory>
#include <lunaricorn/signaling_api.h>
#include <lunaricorn/proto/signaling.h>
#include <lunaricorn.h>

class SignalConnector
{
public:
    struct Config
    {
        std::string host;
        uint16_t req_port;
        uint16_t pub_port;
        std::string api;
        std::string agent_id;
    };

    SignalConnector();
    ~SignalConnector();

    // Initialize with config, returns true on success
    bool initialize(const Config& cfg);

    // Check if connector is ready
    bool ready() const;

    // Stop the connector
    void stop();

    /// Push a signaling event (FileOp_new / FileOp_update)
    bool push_event(const std::string& event_type, const std::string& object_id, const std::string& uuid);

private:
    std::shared_ptr<lunaricorn::SignalingConnector> _signaling;
    std::atomic<bool> _ready{false};
};