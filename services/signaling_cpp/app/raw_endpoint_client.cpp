#include "raw_endpoint_client.h"

#include <chrono>
#include <cstring>
#include <format>

#include "maintenance.h"

using namespace lunaricorn::internal;
using namespace std::chrono_literals;

static constexpr int kBufReservationB = 1024;

namespace lunaricorn
{

RE_Client::RE_Client(Poco::Net::StreamSocket socket, SignalingEnginePtr engine)
    : sock(std::move(socket))
    , _engine(std::move(engine))
{
    _count++;
    _proto = std::make_shared<lunaricorn::internal::SignalingProto>();
    _last_send = std::chrono::steady_clock::now();
    _client_hb = Clock::now();
    _server_hb = Clock::now();
}

RE_Client::~RE_Client()
{
    _count--;
}

RE_Client::RE_Client(RE_Client&& other) noexcept
    : sock(std::move(other.sock))
    , _proto(other._proto)
    , _engine(std::move(other._engine))
    , _last_send(other._last_send)
    , _client_hb(other._client_hb)
    , _server_hb(other._server_hb)
    , _pstate(other._pstate)
{
    _count++;
}

RE_Client& RE_Client::operator=(RE_Client&& other) noexcept
{
    if (this != &other)
    {
        sock = std::move(other.sock);
        _proto = other._proto;
        _engine = std::move(other._engine);
        _last_send = other._last_send;
        _client_hb = other._client_hb;
        _server_hb = other._server_hb;
        _pstate = other._pstate;
    }
    return *this;
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

    // Dispatch to type-specific handler
    switch (header.type)
    {
    case MessageType::MT_HB:
        on_heartbeat(msg);
        break;
    case MessageType::MT_PubReq:
        on_pub_request(msg);
        break;
    case MessageType::MT_QueryReq:
        on_query_request(msg);
        break;
    case MessageType::MT_Sub:
        on_subscription(msg);
        break;
    default:
        MLOG_E("unknown message type: {}", static_cast<int>(header.type));
        break;
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

void RE_Client::on_heartbeat(const IncomingMessage& msg)
{
    update_client_hb();

    MessageHeader hdr = {
        .magic = HeaderMagic,
        .version = PROTOCOL_VERSION,
        .type = MT_HB,
        .data_type = CT_Json,
        .flags = 0,
        .seq = msg.header.seq,
        .data_len = 0,
        .crc = 0
    };

    boost::json::object resp;
    resp["status"] = "ok";

    if (!send_message(hdr, resp))
    {
        MLOG_E("client[{}] failed to send heartbeat response", _id);
    }
}

void RE_Client::on_pub_request(const IncomingMessage& msg)
{
    auto send_resp = [this, seq = msg.header.seq](boost::json::object resp) -> void
    {
        MessageHeader hdr = {
            .magic = HeaderMagic,
            .version = PROTOCOL_VERSION,
            .type = MT_Response,
            .data_type = CT_Json,
            .flags = 0,
            .seq = seq,
            .data_len = 0,
            .crc = 0
        };

        if (!send_message(hdr, resp))
        {
            MLOG_E("client[{}] failed to send pub_response", _id);
        }
    };

    StoredEventData event_data;
    const auto& data = msg.data;

    // Extract required fields
    auto it_type = data.find("event_type");
    if (it_type == data.end() || !it_type->value().is_string())
    {
        send_resp({{"status", "error"}, {"reason", "missing or invalid 'event_type'"}});
        return;
    }
    event_data.event_type = it_type->value().as_string().c_str();

    // NOTE: The "message" key comes from SignalingEvent::toDict() where
    // event payload is serialized under this key. "payload" is NOT used
    // in the SignalingEvent format — keep aligned with toDict().
    auto it_message = data.find("message");
    if (it_message == data.end())
    {
        send_resp({{"status", "error"}, {"reason", "missing 'message'"}});
        return;
    }
    event_data.payload = it_message->value();

    // Extract timestamp (current time if not provided)
    auto it_ts = data.find("timestamp");
    if (it_ts != data.end() && it_ts->value().is_double())
    {
        event_data.timestamp = it_ts->value().as_double();
    }
    else
    {
        event_data.timestamp = std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    // Extract optional fields
    // SignalingEvent::toDict() uses "client_id", not "source".
    // Try both to maintain backward compatibility.
    auto it_source = data.find("client_id");
    if (it_source == data.end() || !it_source->value().is_string())
    {
        it_source = data.find("source");
    }
    if (it_source != data.end() && it_source->value().is_string())
    {
        event_data.source = it_source->value().as_string().c_str();
    }

    auto it_affected = data.find("affected");
    if (it_affected != data.end())
    {
        event_data.affected = it_affected->value();
    }
    else
    {
        event_data.affected = boost::json::value(boost::json::array{});
    }

    auto it_tags = data.find("tags");
    if (it_tags != data.end() && it_tags->value().is_array())
    {
        for (const auto& tag : it_tags->value().as_array())
        {
            if (tag.is_string())
                event_data.tags.push_back(tag.as_string().c_str());
        }
    }

    // Persist event via engine
    try
    {
        long long event_id = _engine->createEvent(event_data);
        send_resp({{"status", "ok"}, {"event_id", event_id}});
    }
    catch (const std::exception& e)
    {
        MLOG_E("client[{}] on_pub_request exception: {}", _id, e.what());
        send_resp({{"status", "error"}, {"reason", std::string("engine error: ") + e.what()}});
    }
}

void RE_Client::on_query_request(const IncomingMessage& msg)
{
    auto send_resp = [this, seq = msg.header.seq](boost::json::object resp) -> void
    {
        MessageHeader hdr = {
            .magic = HeaderMagic,
            .version = PROTOCOL_VERSION,
            .type = MT_Response,
            .data_type = CT_Json,
            .flags = 0,
            .seq = seq,
            .data_len = 0,
            .crc = 0
        };

        if (!send_message(hdr, resp))
        {
            MLOG_E("client[{}] failed to send query_response", _id);
        }
    };

    try
    {
        const auto& data = msg.data;

        // Parse timestamp (default to 0 = from beginning)
        double timestamp = 0.0;
        auto it_ts = data.find("timestamp");
        if (it_ts != data.end() && it_ts->value().is_double())
        {
            timestamp = it_ts->value().as_double();
        }

        // Parse optional filter arrays
        auto extract_strings = [](const boost::json::value& val) -> std::vector<std::string>
        {
            std::vector<std::string> result;
            if (val.is_array())
            {
                for (const auto& item : val.as_array())
                {
                    if (item.is_string())
                        result.push_back(item.as_string().c_str());
                }
            }
            return result;
        };

        std::vector<std::string> types, sources, affected, tags;
        int limit = 0;

        auto it_types = data.find("types");
        if (it_types != data.end()) types = extract_strings(it_types->value());

        auto it_sources = data.find("sources");
        if (it_sources != data.end()) sources = extract_strings(it_sources->value());

        auto it_affected = data.find("affected");
        if (it_affected != data.end()) affected = extract_strings(it_affected->value());

        auto it_tags = data.find("tags");
        if (it_tags != data.end()) tags = extract_strings(it_tags->value());

        auto it_limit = data.find("limit");
        if (it_limit != data.end() && it_limit->value().is_int64())
        {
            limit = static_cast<int>(it_limit->value().as_int64());
        }

        // Execute query via engine
        auto results = _engine->findEvents(timestamp, types, sources, affected, tags, limit);

        // Serialize results to JSON array
        boost::json::array events_arr;
        for (const auto& ev : results)
        {
            boost::json::object ev_obj;
            ev_obj["event_id"] = ev.eid;
            ev_obj["event_type"] = ev.event_type;
            ev_obj["timestamp"] = ev.timestamp;
            ev_obj["payload"] = ev.payload;

            if (ev.source.has_value())
                ev_obj["source"] = *ev.source;

            ev_obj["affected"] = ev.affected;

            boost::json::array tags_arr;
            for (const auto& tag : ev.tags)
                tags_arr.push_back(boost::json::value(tag));
            ev_obj["tags"] = std::move(tags_arr);

            events_arr.push_back(std::move(ev_obj));
        }

        send_resp({{"status", "ok"}, {"events", std::move(events_arr)}, {"count", static_cast<int64_t>(results.size())}});
    }
    catch (const std::exception& e)
    {
        MLOG_E("client[{}] on_query_request exception: {}", _id, e.what());
        send_resp({{"status", "error"}, {"reason", std::string("engine error: ") + e.what()}});
    }
}

void RE_Client::on_subscription(const IncomingMessage& msg)
{
    auto send_resp = [this, seq = msg.header.seq](boost::json::object resp) -> void
    {
        MessageHeader hdr = {
            .magic = HeaderMagic,
            .version = PROTOCOL_VERSION,
            .type = MT_Response,
            .data_type = CT_Json,
            .flags = 0,
            .seq = seq,
            .data_len = 0,
            .crc = 0
        };

        if (!send_message(hdr, resp))
        {
            MLOG_E("client[{}] failed to send sub_response", _id);
        }
    };

    try
    {
        const auto& data = msg.data;

        auto extract_strings = [](const boost::json::value& val) -> std::vector<std::string>
        {
            std::vector<std::string> result;
            if (val.is_array())
            {
                for (const auto& item : val.as_array())
                {
                    if (item.is_string())
                        result.push_back(item.as_string().c_str());
                }
            }
            return result;
        };

        std::vector<std::string> types, sources, affected, tags;

        auto it_types = data.find("types");
        if (it_types != data.end()) types = extract_strings(it_types->value());

        auto it_sources = data.find("sources");
        if (it_sources != data.end()) sources = extract_strings(it_sources->value());

        auto it_affected = data.find("affected");
        if (it_affected != data.end()) affected = extract_strings(it_affected->value());

        auto it_tags = data.find("tags");
        if (it_tags != data.end()) tags = extract_strings(it_tags->value());

        // Check if unsubscribe requested
        auto it_action = data.find("action");
        bool do_unsubscribe = (it_action != data.end() &&
                               it_action->value().is_string() &&
                               it_action->value().as_string() == "unsubscribe");

        if (do_unsubscribe)
        {
            _engine->unsubscribe(_id);
            send_resp({{"status", "ok"}, {"action", "unsubscribed"}});
        }
        else
        {
            _engine->subscribe(_id, types, sources, affected, tags);
            send_resp({{"status", "ok"}, {"action", "subscribed"}});
        }
    }
    catch (const std::exception& e)
    {
        MLOG_E("client[{}] on_subscription exception: {}", _id, e.what());
        send_resp({{"status", "error"}, {"reason", std::string("engine error: ") + e.what()}});
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