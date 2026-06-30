#include "signaling_api.h"
#include <chrono>
#include <random>

using namespace lunaricorn::internal;
using namespace std::chrono_literals; 

static constexpr int kHeartbeatIntervalMs = 1000;
static constexpr int kBufReservationB = 1024;

/// Receive buffer size must be large enough to hold the largest expected
/// message from the server (header + JSON payload). 64KB covers typical
/// subscription event batches and query responses without fragmentation.
static constexpr int kRecvBufSize = 65536;
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
    _proto = std::make_shared<lunaricorn::internal::SignalingProto>();
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
    std::lock_guard<std::mutex> lock(_connection_mutex);
    if (_sock)
    {
        // cleanup broken state
        _sock->close();
        _sock.reset();
    }
    _connected = false;
    _host =  host;
    _raw_port = raw_port;
    
    const Poco::Net::SocketAddress addr(_host, _raw_port);
    _sock = std::make_shared<Poco::Net::StreamSocket>(addr);
    _thread_state = std::make_shared<ThreadState>();  // FIX: store state for stop_runner()
    _runner_thread = std::jthread([this](std::stop_token st){runner(st, _thread_state);});
    {
        std::lock_guard<std::mutex> lock(_last_send_mutex);
        _last_send = std::chrono::steady_clock::now();
    }
    _hb_timer.start(Poco::TimerCallback<SignalingConnector>(*this, &SignalingConnector::onHBTimer));

    _connected = true;
    return _connected;
}
bool SignalingConnector::stop()
{
    if (_stopping)
    {
        // [BUG] recursive stop
        MBUG("SignalingConnector::stop() called recursively");
        return false;
    }
    _stopping = true;
    _connected = false;
    _hb_timer.stop();
    stop_runner();
    std::lock_guard<std::mutex> lock(_connection_mutex);
    if (_sock)
    {
        _sock->close();
        _sock.reset();
    }
    _stopping = false;
    return true;
}

void SignalingConnector::stop_runner()
{
    if (!_runner_thread.has_value())
        return;

    auto& thread = *_runner_thread;
    thread.request_stop();

    if (_thread_state && _thread_state->finished.load(std::memory_order_acquire))
    {
        _runner_thread.reset();
        _thread_state.reset();
        return;
    }

    lunaricorn::internal::OrphanThreadManager::instance().add(
        std::move(thread),
        std::move(_thread_state),
        std::chrono::seconds(10),
        "SignalingConnector::runner"
    );

    _runner_thread.reset();
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

    std::chrono::duration<double> elapsed;
    {
        std::lock_guard<std::mutex> lock(_last_send_mutex);
        elapsed = std::chrono::steady_clock::now() - _last_send;
    }
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
        try
        {
            _disconnectCbk.value()(reason, magic);
        }
        catch (const std::exception& e)
        {
            MLOG_E("on_disconnect callback exception {}", e.what());
        }
    }

    _connected = false;
    _hb_timer.stop();
    if (_runner_thread.has_value())
    {
        _runner_thread->request_stop();
    }
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
            {
                continue;
            }

            _pstate.headerComplete = true;

            if (_pstate.header.magic != HeaderMagic)
            {
                MLOG_E("invalid header magic: expected=0x{:08X} actual=0x{:08X}", HeaderMagic, _pstate.header.magic);
                _pstate.reset();
                continue; // FIX: continue instead of return — try next bytes
            }

            if (_pstate.header.version != PROTOCOL_VERSION)
            {
                MLOG_E("invalid protocol version: expected={} actual={}", static_cast<uint32_t>(PROTOCOL_VERSION), static_cast<uint32_t>(_pstate.header.version));
                _pstate.reset();
                continue;
            }

            if (_pstate.header.data_len > lunaricorn::internal::MAX_DATA_LEN)
            {
                MLOG_E("payload too large: max={} actual={}",lunaricorn::internal::MAX_DATA_LEN,_pstate.header.data_len);
                _pstate.reset();
                continue;
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
                    continue;
                }

                if (!msg.isValid)
                {
                    MLOG_E("invalid parsed message: seq={} reason={}",_pstate.header.seq,msg.errorReason);
                    _pstate.reset();
                    continue;
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
                continue;
            }

            lunaricorn::internal::IncomingMessage msg;

            if (!_proto->deserializeJson(_pstate.buffer, msg))
            {
                MLOG_E("deserializeJson failed: seq={} payload={} bytes",_pstate.header.seq,_pstate.header.data_len);
                _pstate.reset();
                continue;
            }

            if (!msg.isValid)
            {
                MLOG_E("invalid parsed message: seq={} reason={}", _pstate.header.seq, msg.errorReason);
                _pstate.reset();
                continue;
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
    SignalingResponse resp(0);
    {
        std::lock_guard<std::mutex> lock(_pending_responses_mutex);
        auto it = _pending_responses.find(seq);
        if (it == _pending_responses.end())
        {
            on_server_request(msg);
            return;
        }
        resp = it->second;
        _pending_responses.erase(it);
    }
    if (_respCbk)
    {
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
    if (!_subCbk) {return;}
    const auto type = msg.header.type;
    switch (type)
    {
    case MT_Sub:
    {
        SignalingSubEvent sub(0);
        bool buildRc = sub.build(msg.data);
        if (!buildRc) 
        {
            MLOG_E("cannot build SignalingSubEvent message");
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
        break;
    }
    case MT_Response:
    {
        MLOG_D("on server response (not matched via pending)");
        break;
    }
    default:
    {
        MLOG_E("unknown message type: {}", static_cast<int>(type));
        break;
    }
    }
}

void SignalingConnector::runner(std::stop_token stopToken, std::shared_ptr<lunaricorn::internal::ThreadState> state)
{
    if (!state) {MBUG("no state"); return;}
    struct FinishGuard
    {
        std::shared_ptr<lunaricorn::internal::ThreadState> state;
        ~FinishGuard()
        {
            state->finished.store(true, std::memory_order_release);
        }
    } finishGuard{ state };
    static const Poco::Timespan pollTimeout(1000000); // 1s
    std::vector<uint8_t> buffer(kRecvBufSize);
    bool normal_break = true;
    uint64_t magic = 0;
    while (!stopToken.stop_requested())
    {
        normal_break = false;
        magic = random_u64();
        try
        {
            int n = 0;
            {
                std::lock_guard<std::mutex> lock(_connection_mutex);
                if (!_sock)
                {
                    MBUG("no sock object in runner @{}", magic);
                    return;
                }
                if (!_connected)
                {
                    MBUG("not connected @{}", magic);
                    return;
                }
                if (!_sock->poll(pollTimeout, Poco::Net::Socket::SELECT_READ))
                {
                    continue;
                }

                n = _sock->receiveBytes(buffer.data(),static_cast<int>(buffer.size()));
            }
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
        normal_break = true;
    }
    if (normal_break)
        MLOG_D("signaling client thread normal exit");
    else
        MLOG_E("thread emergency exit @{}", magic);

}

bool SignalingConnector::send_message(lunaricorn::internal::MessageHeader& msg,const boost::json::object& data)
{
    std::vector<uint8_t> buf;
    if (!data.empty())
    {
        buf.reserve(sizeof(lunaricorn::internal::MessageHeader) + kBufReservationB);
    }
    if (!_proto)
    {
        MBUG(" no proto object!");
        return false;
    }
    size_t sz = _proto->serializeJson(msg, buf, data);
    if(sz == 0){MBUG("Failed to serialize message"); return false;}
    MLOG_D("try to send {}b", sz);
    {
        std::lock_guard<std::mutex> lock(_last_send_mutex);
        _last_send = std::chrono::steady_clock::now();
    }
    std::lock_guard<std::mutex> lock(_connection_mutex);
    return _proto->send_raw(_sock, buf);
}


bool SignalingConnector::push(const SignalingEvent& event)
{
    const seq_t seq = make_seq();
    SignalingPushRequest msg(seq);
    msg.data = event.toDict();
    if (msg.data.empty()) {MBUG("dict is empty"); return false;}
    lunaricorn::internal::MessageHeader header{};
    msg.make_header(header);
    SignalingResponse resp(seq);
    resp.origin.emplace<SignalingPushRequest>(msg);
    {
        std::lock_guard<std::mutex> lock(_pending_responses_mutex);
        if (_pending_responses.contains(seq))
        {
            MBUG("seq {} already in pending queue. seq is not unic!", seq);
            return false;
        }
        _pending_responses[seq] = resp;
    }
    bool send_rc = send_message(header, msg.data);
    if (send_rc)
    {
        MLOG_D("pushed event with seq {}", seq);
    } else {
        MLOG_E("push event failed seq {}", seq);
    }
    return send_rc;
}

} // namespace lunaricorn