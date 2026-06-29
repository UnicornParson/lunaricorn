#include "raw_endpoint_client.h"

#include <chrono>
#include <random>
#include <cstring>
#include <format>

#include "maintenance.h"

using namespace lunaricorn::internal;
using namespace std::chrono_literals;

static constexpr int kBufReservationB = 1024;
static constexpr auto silence_duration = 5s;

namespace lunaricorn
{

// Random uint64 generator
static uint64_t random_u64()
{
    static std::mt19937_64 engine = []
    {
        std::random_device rd;
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        uint64_t time_seed = static_cast<uint64_t>(now);
        unsigned int low = static_cast<unsigned int>(time_seed);
        unsigned int high = static_cast<unsigned int>(time_seed >> 32);
        std::seed_seq seed{rd(), rd(), rd(), low, high};
        return std::mt19937_64{seed};
    }();
    static std::uniform_int_distribution<uint64_t> dist;
    return dist(engine);
}

RE_Client::RE_Client(Poco::Net::StreamSocket socket)
    : sock(std::move(socket))
{
    _count++;
    _proto = std::make_shared<lunaricorn::internal::SignalingProto>();
    _last_send = std::chrono::steady_clock::now();
}

RE_Client::~RE_Client()
{
    _count--;
}

RE_Client::RE_Client(RE_Client&& other) noexcept
    : sock(std::move(other.sock))
    , _proto(other._proto)
    , _last_send(other._last_send)
    , _seq(other._seq.load())
    , _pstate(other._pstate)
    , _connectTime(other._connectTime)
    , _client_hb(other._client_hb)
    , _server_hb(other._server_hb)
    , _pending_responses(other._pending_responses)
{
    other._seq = 0;
    _count++;
}

RE_Client& RE_Client::operator=(RE_Client&& other) noexcept
{
    if (this != &other)
    {
        sock = std::move(other.sock);
        _proto = other._proto;
        _last_send = other._last_send;
        _seq = other._seq.load();
        _pstate = other._pstate;
        _connectTime = other._connectTime;
        _client_hb = other._client_hb;
        _server_hb = other._server_hb;
        _pending_responses = other._pending_responses;

        other._seq = 0;
    }
    return *this;
}

void RE_Client::send_client_hb()
{
    MessageHeader hb_msg = {
        .magic = HeaderMagic,
        .version = PROTOCOL_VERSION,
        .type = MessageType::MT_HB,
        .data_type = ContentType::CT_Json,
        .flags = 0,
        .seq = 0,  // no response
        .data_len = 0,
        .crc = 0
    };

    bool sendRc = send_message(hb_msg, boost::json::object());
    if (!sendRc)
    {
        MLOG_E("Failed to send HB message");
    }
}

void RE_Client::processData(const std::vector<char>& data)
{
    size_t offset = 0;

    while (offset < data.size())
    {
        if (!_pstate.headerComplete)
        {
            const size_t need = sizeof(MessageHeader) - _pstate.receivedHeaderBytes;
            const size_t avail = data.size() - offset;
            const size_t take = std::min(need, avail);

            std::memcpy(reinterpret_cast<uint8_t*>(&_pstate.header) + _pstate.receivedHeaderBytes,
                        data.data() + offset,
                        take);
            _pstate.receivedHeaderBytes += take;
            offset += take;

            if (_pstate.receivedHeaderBytes < sizeof(MessageHeader))
            {
                continue;
            }

            _pstate.headerComplete = true;

            if (_pstate.header.magic != HeaderMagic)
            {
                MLOG_E("invalid header magic: expected=0x{:08X} actual=0x{:08X}", HeaderMagic, _pstate.header.magic);
                _pstate.reset();
                return;
            }

            if (_pstate.header.version != PROTOCOL_VERSION)
            {
                MLOG_E("invalid protocol version: expected={} actual={}",
                       static_cast<uint32_t>(PROTOCOL_VERSION),
                       static_cast<uint32_t>(_pstate.header.version));
                _pstate.reset();
                return;
            }

            if (_pstate.header.data_len > MAX_DATA_LEN)
            {
                MLOG_E("payload too large: max={} actual={}", MAX_DATA_LEN, _pstate.header.data_len);
                _pstate.reset();
                return;
            }

            _pstate.buffer.resize(sizeof(MessageHeader) + _pstate.header.data_len);

            std::memcpy(_pstate.buffer.data(), &_pstate.header, sizeof(MessageHeader));
            if (_pstate.header.data_len == 0)
            {
                IncomingMessage msg;
                if (!_proto->deserializeJson(_pstate.buffer, msg))
                {
                    MLOG_E("deserializeJson failed for empty payload packet: seq={} type={}",
                           _pstate.header.seq,
                           static_cast<uint32_t>(_pstate.header.type));
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
        }

        if (_pstate.headerComplete)
        {
            const size_t need = _pstate.header.data_len - _pstate.receivedPayloadBytes;
            const size_t avail = data.size() - offset;
            const size_t take = std::min(need, avail);

            std::memcpy(_pstate.buffer.data() + sizeof(MessageHeader) + _pstate.receivedPayloadBytes,
                        data.data() + offset,
                        take);
            _pstate.receivedPayloadBytes += take;
            offset += take;

            if (_pstate.receivedPayloadBytes < _pstate.header.data_len)
                continue;

            const size_t expected = sizeof(MessageHeader) + _pstate.header.data_len;

            if (_pstate.buffer.size() != expected)
            {
                MLOG_E("assembled packet size mismatch: expected={} actual={}", expected, _pstate.buffer.size());
                _pstate.reset();
                return;
            }

            IncomingMessage msg;

            if (!_proto->deserializeJson(_pstate.buffer, msg))
            {
                MLOG_E("deserializeJson failed: seq={} payload={} bytes",
                       _pstate.header.seq,
                       _pstate.header.data_len);
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
    }
}

void RE_Client::on_message(const IncomingMessage& msg)
{
    const MessageHeader& header = msg.header;
    if (!msg.isValid)
    {
        MLOG_E("invalid header: seq={} reason={}", header.seq, msg.errorReason);
        return;
    }

    // Check if this is a response to a pending request
    const seq_t seq = header.seq;

    {
        std::lock_guard<std::mutex> lock(_pending_responses_mutex);
        auto it = _pending_responses.find(seq);
        if (it == _pending_responses.end())
        {
            on_server_request(msg);
            return;
        }
        SignalingResponse resp = it->second;
        _pending_responses.erase(it);

        resp.ok = msg.isValid;
        resp.data = msg.data;
        resp.error = msg.errorReason;

        // Note: response callback would need to be added for server-side mode
        // For now, we just forward to the message callback
    }

    // Forward parsed message to server via callback
    if (_msgCbk)
    {
        try
        {
            _msgCbk(_id, msg);
        }
        catch (const std::exception& e)
        {
            MLOG_E("message callback exception {}", e.what());
        }
    }
}

void RE_Client::on_server_request(const IncomingMessage& msg)
{
    const auto type = msg.header.type;

    switch (type)
    {
    case MessageType::MT_HB:
    {
        MLOG_D("on server hb");
        // Client heartbeat received, update timestamp
        update_client_hb();
        break;
    }

    default:
    {
        MLOG_E("unknown message type: {}", static_cast<int>(type));
        break;
    }
    }
}

bool RE_Client::send_message(MessageHeader& msg, const boost::json::object& data)
{
    std::vector<uint8_t> buf;
    if (!data.empty())
    {
        buf.reserve(sizeof(MessageHeader) + kBufReservationB); // optimistic allocation
    }

    if (!_proto)
    {
        MBUG("no proto object!");
        return false;
    }

    size_t sz = _proto->serializeJson(msg, buf, data);
    if (sz == 0)
    {
        MBUG("Failed to serialize message");
        return false;
    }

    MLOG_D("try to send {}b", sz);

    {
        std::lock_guard<std::mutex> lock(_last_send_mutex);
        _last_send = std::chrono::steady_clock::now();
    }

    try 
    {
        int bytesSent = sock.sendBytes(buf.data(), static_cast<int>(buf.size()));
        return bytesSent == static_cast<int>(buf.size());
    }
    catch (const Poco::Exception& e)
    {
        MLOG_E("send_bytes exception: {}", e.displayText());
        return false;
    }
}

} // namespace lunaricorn