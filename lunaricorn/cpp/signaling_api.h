#pragma once
#include "stdafx.h"
#include <Poco/Timer.h>
#include <Poco/Thread.h>
#include <Poco/Stopwatch.h>
#include <Poco/Net/SocketStream.h>
#include <Poco/URI.h>
#include <thread>
#include <future>
#include <span>
#include "maintenance.h"
#include "proto/signaling.h"
#include "lunaricorn.h"
#include "internal/thread_control.h"
namespace lunaricorn
{

using seq_t = uint64_t;

class SignalingMessage
{
public:
    explicit SignalingMessage(seq_t seq) : _seq(seq){}
    SignalingMessage(const SignalingMessage&) = default;
    SignalingMessage(SignalingMessage&&) = default;
    SignalingMessage& operator=(const SignalingMessage&) = default;
    SignalingMessage& operator=(SignalingMessage&&) = default;
    auto operator<=>(const SignalingMessage& other) const {return _seq <=> other._seq;}
    seq_t _seq = 0;
};


class SignalingSubEvent: public SignalingMessage
{
public:
    explicit SignalingSubEvent(seq_t seq) : SignalingMessage(seq){}
    bool build(const boost::json::object& data);
    std::vector<lunaricorn::internal::SignalingEvent> events;
};

class SignalingPushRequest: public SignalingMessage
{
public:
    explicit SignalingPushRequest(seq_t seq) : SignalingMessage(seq){}
    SignalingPushRequest() : SignalingMessage(0){}
    SignalingPushRequest(const SignalingPushRequest&) = default;
    SignalingPushRequest(SignalingPushRequest&&) = default;
    SignalingPushRequest& operator=(const SignalingPushRequest&) = default;


    void make_header(lunaricorn::internal::MessageHeader& header) const;
    boost::json::object data;
};

class SignalingPullRequest: public SignalingMessage
{
public:
    explicit SignalingPullRequest(seq_t seq) : SignalingMessage(seq){}
};
class SignalingResponse: public SignalingMessage
{
public:
    SignalingResponse() : SignalingMessage(0){}
    explicit SignalingResponse(seq_t seq) : SignalingMessage(seq){}
    SignalingResponse(const SignalingResponse&) = default;
    SignalingResponse(SignalingResponse&&) = default;
    SignalingResponse& operator=(const SignalingResponse&) = default;
    bool ok = false;
    std::string error;
    boost::json::object data;
    std::variant<std::monostate, SignalingPushRequest, SignalingPullRequest> origin;
};


class SignalingConnector
{
public:
    using ResponseCallback = std::function<void(const SignalingResponse&)>;
    using SubscriptionCallback = std::function<void(const SignalingSubEvent&)>;
    using DisconnectCallback = std::function<void(const std::string& reason, uint64_t magic)>; // disconnect reason and random token for grep
    using ResponseCallbackOpt = std::optional<ResponseCallback>;
    using SubscriptionCallbackOpt = std::optional<SubscriptionCallback>;
    using DisconnectCallbackOpt = std::optional<DisconnectCallback>;


    SignalingConnector();
    ~SignalingConnector();
    bool start(const std::string& host, Poco::UInt16 raw_port);
    bool stop();
    bool ready();
    void onHBTimer(Poco::Timer&);

    inline void set_response_callback(const ResponseCallbackOpt& callback) { _respCbk = callback; }
    inline void set_subscription_callback(const SubscriptionCallbackOpt& callback)  { _subCbk = callback; }
    inline void set_disconnect_callback(const DisconnectCallbackOpt& callback) { _disconnectCbk = callback; }


    bool push(const lunaricorn::internal::SignalingEvent& event);
private:
struct IncomingPacketState
{
    using PacketHeader = lunaricorn::internal::MessageHeader;
    static constexpr size_t kHeaderSize = sizeof(PacketHeader);

    PacketHeader header {};
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

    void runner(std::stop_token stopToken, std::shared_ptr<lunaricorn::internal::ThreadState> state); // thread function
    void stop_runner();
    bool send_message(lunaricorn::internal::MessageHeader& msg,const boost::json::object& data);
    void send_client_hb();
    void on_disconnect(const std::string& reason, uint64_t magic);
    void on_data(std::span<const uint8_t> data);
    void on_message(const lunaricorn::internal::IncomingMessage& msg);
    void on_server_request(const lunaricorn::internal::IncomingMessage& msg);
    seq_t make_seq();

    Poco::Timer _hb_timer;
    std::chrono::steady_clock::time_point _last_send;
    std::mutex _last_send_mutex;
    std::mutex _connection_mutex;
    IncomingPacketState _pstate;
    std::atomic<bool> _connected {false};
    std::atomic<bool> _stopping {false};
    std::optional<std::jthread>  _runner_thread;
    std::shared_ptr<lunaricorn::internal::ThreadState> _thread_state;

    std::string _host;
    Poco::UInt16 _raw_port;
    std::shared_ptr<Poco::Net::StreamSocket> _sock;
    std::shared_ptr<lunaricorn::internal::SignalingProto> _proto;
    std::atomic<seq_t> _seq;

    std::map<seq_t, SignalingResponse> _pending_responses;
    std::mutex _pending_responses_mutex;

    ResponseCallbackOpt _respCbk;
    SubscriptionCallbackOpt _subCbk;
    DisconnectCallbackOpt _disconnectCbk;

}; // class SignalingConnector
} // namespace lunaricorn




/*
    def push_event(self, event_type: str, payload: Dict[str, Any], 
        source: Optional[str] = None, tags: Optional[List] = None) -> Dict[str, Any]:
"""
Send a push event to the server.

:param event_type: Type of the event
:param payload: Event payload data
:param source: Source of the event (optional)
:param tags: Event tags (optional)
:return: Server response dictionary
"""
if not self.connected:
  raise ConnectionError("Client is not connected to server")
  
message = {
  "type": "push",
  "client_id": self.client_id,
  "event_type": event_type,
  "message": payload,
  "timestamp": time.time()
}

if source:
  message["source"] = source
  
if tags:
  message["tags"] = tags
  
return self._send_request(message)
*/