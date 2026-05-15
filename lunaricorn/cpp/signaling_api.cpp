#include "signaling_api.h"


static constexpr int kHeartbeatIntervalMs = 1000;
static constexpr int kBufReservationB = 1024;

static constexpr int kRecvBufSize = sizeof(lunaricorn::internal::MessageHeader) + 2;

using namespace lunaricorn::internal;

namespace lunaricorn
{
SignalingConnector::SignalingConnector() :
_hb_timer(kHeartbeatIntervalMs, kHeartbeatIntervalMs),
_raw_port (0)
{

}
SignalingConnector::~SignalingConnector()
{

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

void SignalingConnector::onHBTimer(Poco::Timer&)
{
    if (!ready()) {return;}
}

void SignalingConnector::on_disconnect(const std::string& reason)
{
    if (_disconnectCbk)
    {
        _disconnectCbk.value()(reason);
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
    const boost::json::object& data = msg.data;
    const auto seq = header.seq;
    if (!msg.isValid)
    {
        MLOG_E("invalid header: seq={} reason={}", header.seq, msg.errorReason);
        return;
    }
    auto it = _pending_responces.find(seq);
    if (it == _pending_responces.end())
    {
        on_server_request(msg, data);
        return;
    }





}

void SignalingConnector::on_server_request(const lunaricorn::internal::IncomingMessage& msg, const boost::json::object& data)
{

}

void SignalingConnector::runner(std::stop_token stopToken)
{
    static const Poco::Timespan pollTimeout(100000); // 100 ms
    std::vector<uint8_t> buffer(kRecvBufSize);

    while (!stopToken.stop_requested())
    {
        if (!_sock)
        {
            MLOG(MBUG "no sock object in runner");
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
                on_disconnect();
                break;
            }

            MLOG_E("receiveBytes returned invalid size {}", n);
            on_disconnect();
            break;
        }
        catch (const Poco::Exception& e)
        {
            MLOG_E("socket exception: {}", e.displayText());
            on_disconnect();
            break;
        }
        catch (const std::exception& e)
        {
            MLOG_E("exception: {}", e.what());
            on_disconnect();
            break;
        }
        catch (...)
        {
            MLOG_E("unknown exception");
            on_disconnect();
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
    return _proto->send_raw(_sock, buf);
}

} // namespace lunaricorn