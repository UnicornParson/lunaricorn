#include "signaling_api.h"
#include <chrono>
#include <random>

using namespace lunaricorn::internal;
using namespace std::chrono_literals; 

static constexpr int kHeartbeatIntervalMs = 1000;
static constexpr int kBufReservationB = 1024;

static constexpr int kRecvBufSize = sizeof(lunaricorn::internal::MessageHeader) + 2;
static constexpr auto silence_duration = 5s; 

static uint64_t random_u64()
{
    static std::mt19937_64 engine = [] 
    {
        std::random_device rd;
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        uint64_t time_seed = static_cast<uint64_t>(now);
        unsigned int low  = static_cast<unsigned int>(time_seed);
        unsigned int high = static_cast<unsigned int>(time_seed >> 32);
        std::seed_seq seed{rd(), rd(), rd(), low, high};
        return std::mt19937_64{seed};
    }();
    static std::uniform_int_distribution<uint64_t> dist;
    return dist(engine);
}

namespace lunaricorn
{
SignalingConnector::SignalingConnector() :
_hb_timer(kHeartbeatIntervalMs, kHeartbeatIntervalMs),
_raw_port (0)
{
    _last_send = std::chrono::steady_clock::now();
}
SignalingConnector::~SignalingConnector()
{
    stop();
}
bool SignalingConnector::start(const std::string& host, Poco::UInt16 raw_port)
{
    if (_connected || _runner_thread.has_value()) 
    {
        MLOG_E("SignalingConnector already connected");
        return false;

    }

    if (host.empty() || raw_port == 0)
    {
        MLOG_E("Invalid host or port {}:{}", host, raw_port);
        return false;
    }
    if (_sock)
    {
        // cleanup broken state
        _sock->close();
        _sock.reset();
    }
    _connected = false;
    _host =  host;
    _raw_port = raw_port;
    std::lock_guard<std::mutex> lock(_connection_mutex);
    const Poco::Net::SocketAddress addr(_host, _raw_port);
    _sock = std::make_shared<Poco::Net::StreamSocket>(addr);

    _runner_thread = std::jthread([this](std::stop_token st){runner(st);});
    _last_send = std::chrono::steady_clock::now();
    _hb_timer.start(Poco::TimerCallback<SignalingConnector>(*this, &SignalingConnector::onHBTimer));

    _connected = true;
    return _connected;
}
bool SignalingConnector::stop()
{
    if (_stopping)
    {
        // [BUG] recursive stop
        MLOG_E(MBUG "SignalingConnector::stop() called recursively");
        return false;
    }
    _stopping = true;
    _connected = false;
    _hb_timer.stop();
    if (_runner_thread.has_value())
    {
        _runner_thread->request_stop();
        _runner_thread->join();
        _runner_thread.reset();
    }
    if(_sock)
    {
        _sock->close();
        _sock.reset();
    }
    _stopping = false;
    return true;
}
bool SignalingConnector::ready()
{
    return _connected && _sock && _sock->impl()->initialized();
}

seq_t SignalingConnector::make_seq()
{
    seq_t s = _seq;
    ++_seq;
    return s;
}

void SignalingConnector::onHBTimer(Poco::Timer&)
{
    if (!ready()) {return;}
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - _last_send;
    if (elapsed >= silence_duration)
    {
        send_client_hb();
    }
}
void SignalingConnector::send_client_hb()
{
    lunaricorn::internal::MessageHeader hb_msg = {
        .magic = lunaricorn::internal::HeaderMagic,
        .version = lunaricorn::internal::PROTOCOL_VERSION,
        .type = lunaricorn::internal::MessageType::MT_HB,
        .data_type = lunaricorn::internal::ContentType::CT_Json,
        .flags = 0,
        .seq = 0, // no responce
        .data_len = 0,
        .crc = 0
    };
    bool sendRc = send_message(hb_msg, boost::json::object());
    if (!sendRc)
    {
        MLOG_E("Failed to send HB message");
    }

}
void SignalingConnector::on_disconnect(const std::string& reason, uint64_t magic)
{
    if (_disconnectCbk)
    {
        _disconnectCbk.value()(reason, magic);
    }
    stop();
}

void SignalingConnector::on_data(std::span<const uint8_t> data)
{
    size_t offset = 0;

    while (offset < data.size())
    {
        if (!_pstate.headerComplete)
        {
            const size_t need = sizeof(lunaricorn::internal::MessageHeader) - _pstate.receivedHeaderBytes;
            const size_t avail = data.size() - offset;
            const size_t take = std::min(need, avail);

            std::memcpy( reinterpret_cast<uint8_t*>(&_pstate.header) + _pstate.receivedHeaderBytes, data.data() + offset, take);
            _pstate.receivedHeaderBytes += take;
            offset += take;

            if (_pstate.receivedHeaderBytes < sizeof(lunaricorn::internal::MessageHeader))
                continue;

            _pstate.headerComplete = true;

            if (_pstate.header.magic != HeaderMagic)
            {
                MLOG_E("invalid header magic: expected=0x{:08X} actual=0x{:08X}", HeaderMagic, _pstate.header.magic);
                _pstate.reset();
                return;
            }

            if (_pstate.header.version != PROTOCOL_VERSION)
            {
                MLOG_E("invalid protocol version: expected={} actual={}", static_cast<uint32_t>(PROTOCOL_VERSION), static_cast<uint32_t>(_pstate.header.version));
                _pstate.reset();
                return;
            }

            if (_pstate.header.data_len > lunaricorn::internal::MAX_DATA_LEN)
            {
                MLOG_E("payload too large: max={} actual={}",lunaricorn::internal::MAX_DATA_LEN,_pstate.header.data_len);
                _pstate.reset();
                return;
            }

            _pstate.buffer.resize(sizeof(MessageHeader) + _pstate.header.data_len);

            std::memcpy(_pstate.buffer.data(), &_pstate.header, sizeof(MessageHeader));
            if (_pstate.header.data_len == 0)
            {
                lunaricorn::internal::IncomingMessage msg;
                if (!_proto->deserializeJson(_pstate.buffer, msg))
                {
                    MLOG_E("deserializeJson failed for empty payload packet: seq={} type={}",_pstate.header.seq,static_cast<uint32_t>(_pstate.header.type));
                    _pstate.reset();
                    return;
                }

                if (!msg.isValid)
                {
                    MLOG_E("invalid parsed message: seq={} reason={}",_pstate.header.seq,msg.errorReason);
                    _pstate.reset();
                    return;
                }
                on_message(msg);
                _pstate.reset();
            }
        }

        if (_pstate.headerComplete)
        {
            const size_t need = _pstate.header.data_len - _pstate.receivedPayloadBytes;
            const size_t avail = data.size() - offset;
            const size_t take = std::min(need, avail);
            std::memcpy(_pstate.buffer.data() + sizeof(MessageHeader) + _pstate.receivedPayloadBytes, data.data() + offset, take);
            _pstate.receivedPayloadBytes += take;
            offset += take;

            if (_pstate.receivedPayloadBytes < _pstate.header.data_len)
                continue;

            const size_t expected = sizeof(MessageHeader) + _pstate.header.data_len;

            if (_pstate.buffer.size() != expected)
            {
                MLOG_E("assembled packet size mismatch: expected={} actual={}",expected,_pstate.buffer.size());
                _pstate.reset();
                return;
            }

            lunaricorn::internal::IncomingMessage msg;

            if (!_proto->deserializeJson(_pstate.buffer, msg))
            {
                MLOG_E("deserializeJson failed: seq={} payload={} bytes",_pstate.header.seq,_pstate.header.data_len);
                _pstate.reset();
                return;
            }

            if (!msg.isValid)
            {
                MLOG_E("invalid parsed message: seq={} reason={}", _pstate.header.seq, msg.errorReason);
                _pstate.reset();
                return;
            }
            on_message(msg);
            _pstate.reset();
        }
    } // while
} // SignalingConnector::on_data

void SignalingConnector::on_message(const lunaricorn::internal::IncomingMessage& msg)
{
    const lunaricorn::internal::MessageHeader& header = msg.header;
    const auto seq = header.seq;
    if (!msg.isValid)
    {
        MLOG_E("invalid header: seq={} reason={}", header.seq, msg.errorReason);
        return;
    }
    auto it = _pending_responses.find(seq);
    if (it == _pending_responses.end())
    {
        on_server_request(msg);
        return;
    }
    if (_respCbk)
    {
        SignalingResponse resp = it->second;
        resp.ok = msg.isValid;
        resp.data = msg.data;
        resp.error = msg.errorReason;
        try
        {
            _respCbk.value()(resp);
        }
        catch (const std::exception& e)
        {
            MLOG_E("response processing: exception occurred: {}", e.what());
            return;
        }
    }
}

void SignalingConnector::on_server_request(const lunaricorn::internal::IncomingMessage& msg)
{
    // noone is interested in this request.
    if (!_subCbk) {return;}
    const auto type = msg.header.type;
    switch (type)
    {
    case MT_Sub:
    {
        // handle subscription request.
        SignalingSubEvent sub(0);
        bool buildRc = sub.build(msg.data);
        if (buildRc) 
        {
            MLOG_E("cannor build SignalingSubEvent message");
            return;
        }
        try
        {
            _subCbk.value()(sub);
        }
        catch(const std::exception& e)
        {
            MLOG_E("subscribe processing: exception occurred: {}", e.what());
            return;
        }
        break;
    }
    case MT_HB:
    {
        MLOG_D("on server hb");
        send_client_hb();
        break;
    }
    default:
    {
        MLOG_E("unknown message type: {}", static_cast<int>(type));
        break;
    }
    } // switch (type)
}

void SignalingConnector::runner(std::stop_token stopToken)
{
    static const Poco::Timespan pollTimeout(100000); // 100 ms
    std::vector<uint8_t> buffer(kRecvBufSize);

    while (!stopToken.stop_requested())
    {
        const uint64_t magic = random_u64();
        if (!_sock)
        {
            MLOG(MBUG "no sock object in runner @{}", magic);
            return;
        }

        try
        {
            if (!_sock->poll(pollTimeout, Poco::Net::Socket::SELECT_READ))
            {
                continue;
            }

            int n = _sock->receiveBytes(buffer.data(),static_cast<int>(buffer.size()));
            if (n > 0)
            {
                on_data(std::span<const uint8_t>(buffer.data(), static_cast<size_t>(n)));
                continue;
            } else if (n == 0) {
                on_disconnect("read error _0", magic);
                break;
            }

            MLOG_E("receiveBytes returned invalid size {} @{}", n, magic);
            on_disconnect("read error _1", magic);
            break;
        }
        catch (const Poco::Exception& e)
        {
            MLOG_E("socket exception: {} @{}", e.displayText(), magic);
            on_disconnect("socket exception _2", magic);
            break;
        }
        catch (const std::exception& e)
        {
            MLOG_E("exception: {} @{}", e.what(), magic);
            on_disconnect("socket exception _3", magic);
            break;
        }
        catch (...)
        {
            MLOG_E("unknown exception @{}", magic);
            on_disconnect("unknown exception", magic);
            break;
        }
    }
    MLOG_D("signalong clinet thread normal exit");
}

bool SignalingConnector::send_message(lunaricorn::internal::MessageHeader& msg,const boost::json::object& data)
{
    std::vector<uint8_t> buf;
    if (data.empty())
    {
        buf.resize(sizeof(lunaricorn::internal::MessageHeader));
    } else {
        buf.reserve(sizeof(lunaricorn::internal::MessageHeader) + kBufReservationB); // optimistic allocation
    }
    if (!_proto)
    {
        MLOG(MBUG " no proto object!");
        return false;
    }
    size_t sz = _proto->serializeJson(msg, buf, data);
    if(sz == 0){MLOG_E(MBUG "Failed to serialize message"); return false;}
    MLOG_D("try to send {}b", sz);
    std::lock_guard<std::mutex> lock(_connection_mutex);
    _last_send = std::chrono::steady_clock::now();
    return _proto->send_raw(_sock, buf);
}

//---------- events


bool SignalingSubEvent::build(const boost::json::object& data)
{
    auto it = data.find("events");
    if (it == data.end())
    {
        std::stringstream ss;
        for (auto const& field : data) {
            ss << field.key() << ", ";
        }
        MLOG_E("No 'events' field in message. found keys: {}", ss.str());
        return false;
        
    }

    const auto& eventsVal = it->value();
    events.clear();

    if (eventsVal.is_object()) {
        SignalingEvent evt;
        if (!evt.fromDict(eventsVal.as_object()))
        {
            MLOG_E("cannot load SignalingEvent object from elem");
            return false;
        }
        events.push_back(std::move(evt));
        return true;
    }

    if (eventsVal.is_array())
    {
        const auto& arr = eventsVal.as_array();
        int i = 0;
        auto count = arr.size();
        for (const auto& elem : arr)
        {
            if (!elem.is_object())
            {
                MLOG_E("element {}/{} in events array is not object", i, count);
                return false;
            }
            SignalingEvent evt;
            if (!evt.fromDict(elem.as_object()))
            {
                MLOG_E("cannot load SignalingEvent object from {}/{} elem", i, count);
                return false;
            }
            events.push_back(std::move(evt));
            ++i;
        }
        return true;
    }
    MLOG_E(MBUG "'events' shoud be an event object or events array");
    return false;
}


} // namespace lunaricorn