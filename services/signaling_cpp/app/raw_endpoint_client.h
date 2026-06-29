#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <atomic>
#include <mutex>
#include <vector>
#include <chrono>
#include <functional>
#include <optional>
#include <memory>
#include <span>

#include <Poco/Net/StreamSocket.h>
#include <Poco/Timespan.h>

#include "endpoint.h"
#include "maintenance.h"
#include <proto/signaling.h>
#include <signaling_api.h>

namespace lunaricorn
{

// Callback types for server-side client processing
using MessageReadyCallback = std::function<void(uint64_t clientId, const lunaricorn::internal::IncomingMessage& msg)>;
using ClientDisconnectCallback = std::function<void(uint64_t clientId, const std::string& reason)>;

class RE_Client
{
public:
    using Clock = std::chrono::steady_clock;

    // Socket is transferred during construction (server-side mode)
    explicit RE_Client(Poco::Net::StreamSocket socket);
    ~RE_Client();

    // Disable copy
    RE_Client(const RE_Client&) = delete;
    RE_Client& operator=(const RE_Client&) = delete;

    // Enable move
    RE_Client(RE_Client&& other) noexcept;
    RE_Client& operator=(RE_Client&& other) noexcept;

    static inline int64_t clients_count() { return _count.load(std::memory_order_relaxed); }

    // Timing helpers (used by server for heartbeat tracking)
    inline auto connect_time_delay() const { return Clock::now() - _connectTime; }
    inline auto client_hb_delay() const { return Clock::now() - _client_hb; }
    inline auto server_hb_delay() const { return Clock::now() - _server_hb; }
    inline void update_connect_time() { _connectTime = Clock::now(); }
    inline void update_client_hb() { _client_hb = Clock::now(); }
    inline void update_server_hb() { _server_hb = Clock::now(); }

    // Set callbacks for server to receive parsed messages
    inline void set_message_callback(MessageReadyCallback cb) { _msgCbk = std::move(cb); }
    inline void set_disconnect_callback(ClientDisconnectCallback cb) { _disconnectCbk = std::move(cb); }

    // Process incoming raw data from server (called by RawEndpoint::handleClients)
    // This accumulates data and parses complete messages
    void processData(const std::vector<char>& data);

    // Send heartbeat to client (called by server)
    void send_client_hb();

    // Send a message to client
    bool send_message(lunaricorn::internal::MessageHeader& msg, const boost::json::object& data);

    // Check if client is still alive based on silence duration
    inline bool is_silent(std::chrono::seconds threshold) const
    {
        std::chrono::duration<double> elapsed = Clock::now() - _last_send;
        return elapsed >= threshold;
    }

    // Get access to socket for server-side operations
    inline Poco::Net::StreamSocket& socket() { return sock; }
    inline const Poco::Net::StreamSocket& socket() const { return sock; }

    // Set client ID (called by server after creation)
    inline void set_id(uint64_t id) { _id = id; }

    // Get client ID
    inline uint64_t get_id() const { return _id; }

private:
    // Process a complete parsed message
    void on_message(const lunaricorn::internal::IncomingMessage& msg);

    // Process server-initiated request (from client perspective)
    void on_server_request(const lunaricorn::internal::IncomingMessage& msg);

    // Handle disconnect
    void on_disconnect(const std::string& reason);

    // Incoming packet state for incremental parsing
    struct IncomingPacketState
    {
        using PacketHeader = lunaricorn::internal::MessageHeader;
        static constexpr size_t kHeaderSize = sizeof(PacketHeader);

        PacketHeader header{};
        std::vector<uint8_t> buffer;
        size_t receivedHeaderBytes = 0;
        size_t receivedPayloadBytes = 0;
        bool headerComplete = false;

        inline void reset()
        {
            header = {};
            buffer.clear();
            receivedHeaderBytes = 0;
            receivedPayloadBytes = 0;
            headerComplete = false;
        }
    }; // struct IncomingPacketState

    // Connection tracking
    std::chrono::time_point<Clock> _connectTime;
    std::chrono::time_point<Clock> _client_hb;
    std::chrono::time_point<Clock> _server_hb;

    // Last send timestamp
    std::chrono::steady_clock::time_point _last_send;
    std::mutex _last_send_mutex;

    // Sequence counter
    std::atomic<seq_t> _seq{0};

    // Pending responses queue
    std::map<seq_t, SignalingResponse> _pending_responses;
    std::mutex _pending_responses_mutex;

    // Socket (owned by server, but accessible)
    Poco::Net::StreamSocket sock;

    // Protocol handler
    std::shared_ptr<lunaricorn::internal::SignalingProto> _proto;

    // Incoming packet state
    IncomingPacketState _pstate;

    // Callbacks
    MessageReadyCallback _msgCbk;
    ClientDisconnectCallback _disconnectCbk;

    // Client ID (assigned by server)
    uint64_t _id = 0;

    // Connection tracking
    static inline std::atomic<int64_t> _count = 0;
}; // class RE_Client

using RE_Client_ptr = std::shared_ptr<RE_Client>;

} // namespace lunaricorn
