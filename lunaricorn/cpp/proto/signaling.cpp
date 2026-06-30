#include "signaling.h"
#include <vector>
#include <cstdint>
#include <string>
#include <cstring>
#include <format>
#include <boost/json.hpp>
#include <boost/crc.hpp>
#include "../lunaricorn.h"
namespace lunaricorn {
namespace internal {

bool SignalingProto::send_raw(std::shared_ptr<Poco::Net::StreamSocket> sock, const std::vector<uint8_t>& data)
{
    if (data.empty()){return true;}
    if (data.size() >= MAX_DATA_LEN) { MLOG_E("Data too large, sz: {}b", data.size()); return false; }
    if (!sock)
    {
        MBUG(" no sock object!");
        return false;
    }
    int sent = sock->sendBytes(data.data(), data.size());
    if (sent != static_cast<int>(data.size()))
    {
        MLOG_E("Failed to send data, sent: {}, expected: {}", sent, data.size());
        return false;
    }
    return true;
}

size_t SignalingProto::serializeJson(MessageHeader& msg, std::vector<uint8_t>& buf, const boost::json::object& data)
{
    MessageHeader hdr = msg;
    hdr.data_type = CT_Json;
    const uint8_t* hdr_ptr = reinterpret_cast<const uint8_t*>(&hdr);
    size_t json_len = 0;
    std::string json_str;
    if (data.empty())
    {
        hdr.crc = 0; // No data, no CRC
        hdr.data_len = 0;
    } else {
        json_str = boost::json::serialize(data);
        json_len = json_str.size();
        if (json_len > MAX_DATA_LEN) 
        {
            throw std::length_error("Serialized JSON size exceeds MAX_DATA_LEN");
        }
        boost::crc_32_type crc_calculator;
        crc_calculator.process_bytes(json_str.data(), json_len);
        hdr.crc = crc_calculator.checksum();
        hdr.data_len = static_cast<uint32_t>(json_len);
    }
    
    buf.insert(buf.end(), hdr_ptr, hdr_ptr + sizeof(hdr));
    if (json_len > 0)
    {
        buf.insert(buf.end(), json_str.begin(), json_str.end());
    }

    return sizeof(hdr) + json_len;
} // serializeJson


bool SignalingProto::deserializeJson(const std::vector<uint8_t>& buf, IncomingMessage& out)
{
    if (buf.size() < sizeof(MessageHeader)) {
        out.isValid = false;
        out.errorReason = std::format("Buffer too small: got {} bytes, need at least {}", buf.size(), sizeof(MessageHeader));
        s_.fails++; return false;
    }

    MessageHeader hdr;
    std::memcpy(&hdr, buf.data(), sizeof(hdr));

    if (hdr.magic != HeaderMagic) {
        out.isValid = false;
        out.errorReason = std::format("Magic mismatch: expected 0x{:08X}, got 0x{:08X}", HeaderMagic, hdr.magic);
        s_.fails++; return false;
    }

    if (hdr.version != PROTOCOL_VERSION) {
        out.isValid = false;
        out.errorReason = std::format("Version mismatch: expected {}, got {}", PROTOCOL_VERSION, hdr.version);
        s_.fails++; return false;
    }

    if (hdr.data_type != CT_Json) {
        out.isValid = false;
        out.errorReason = std::format("Content type mismatch: expected CT_Json ({}), got {}", static_cast<int>(CT_Json), static_cast<int>(hdr.data_type));
        s_.fails++; return false;
    }

    if (hdr.data_len > MAX_DATA_LEN) {
        out.isValid = false;
        out.errorReason = std::format("Data length {} exceeds maximum allowed {}", hdr.data_len, MAX_DATA_LEN);
        s_.fails++; return false;
    }

    if (buf.size() < sizeof(MessageHeader) + hdr.data_len) {
        out.isValid = false;
        out.errorReason = std::format("Incomplete data: header declares {} bytes, but buffer only has {} bytes available", 
                                        hdr.data_len, buf.size() - sizeof(MessageHeader));
                                        s_.fails++; return false;
    }
    if (hdr.data_len == 0)
    {
        // no data. no src check
        out.header = hdr;
        out.data = boost::json::object();
        out.isValid = true;
        out.errorReason.clear();
        s_.ok++;
        return true;

    }
    const char* json_data = reinterpret_cast<const char*>(buf.data() + sizeof(MessageHeader));
    size_t json_len = hdr.data_len;

    boost::crc_32_type crc_calculator;
    crc_calculator.process_bytes(json_data, json_len);
    uint32_t calculated_crc = crc_calculator.checksum();
    if (calculated_crc != hdr.crc) {
        out.isValid = false;
        out.errorReason = std::format("CRC32 mismatch: expected 0x{:08X}, calculated 0x{:08X}", hdr.crc, calculated_crc);
        s_.fails++; return false;
    }

    try {
        boost::json::value parsed = boost::json::parse({json_data, json_len});
        if (!parsed.is_object()) {
            out.isValid = false;
            out.errorReason = std::format("JSON payload is not an object (type: {})", static_cast<int>(parsed.kind()));
            s_.fails++; return false;
        }
        out.header = hdr;
        out.data = parsed.as_object();
        out.isValid = true;
        out.errorReason.clear();
    } catch (const boost::system::system_error& e) {
        out.isValid = false;
        out.errorReason = std::format("JSON parse error (Boost.System): {}", e.what());
        s_.fails++; return false;
    } catch (const std::exception& e) {
        out.isValid = false;
        out.errorReason = std::format("JSON parse error: {}", e.what());
        s_.fails++; return false;
    } catch (...) {
        out.isValid = false;
        out.errorReason = "Unknown JSON parse error";
        s_.fails++; return false;
    }
    s_.ok++;
    return true;
} // deserializeJson

bool SignalingEvent::fromDict(const boost::json::object& data)
{
    try
    {
        auto getString = [&](const char* key, std::string& out) -> bool
        {
            auto it = data.find(key);

            if (it == data.end())
            {
                MLOG_E("SignalingEvent::fromDict missing required key '{}'", key);
                return false;
            }

            if (!it->value().is_string())
            {
                MLOG_E("SignalingEvent::fromDict key '{}' expected string but got type '{}' value '{}'",
                    key,
                    boost::json::to_string(it->value().kind()),
                    boost::json::serialize(it->value()));
                return false;
            }

            out = it->value().as_string().c_str();
            return true;
        };

        if (!getString("event_type", type))
            return false;

        if (!getString("client_id", source))
            return false;

        auto msgIt = data.find("message");

        if (msgIt == data.end())
        {
            MLOG_E("SignalingEvent::fromDict missing required key 'message'");
            return false;
        }

        if (!msgIt->value().is_object())
        {
            MLOG_E("SignalingEvent::fromDict key 'message' expected object but got type '{}' value '{}'",
                boost::json::to_string(msgIt->value().kind()),
                boost::json::serialize(msgIt->value()));
            return false;
        }

        payload = msgIt->value().as_object();

        auto tsIt = data.find("timestamp");

        if (tsIt == data.end())
        {
            MLOG_E("SignalingEvent::fromDict missing required key 'timestamp'");
            return false;
        }

        double ts = 0.0;

        if (tsIt->value().is_double())
        {
            ts = tsIt->value().as_double();
        }
        else if (tsIt->value().is_int64())
        {
            ts = static_cast<double>(tsIt->value().as_int64());
        }
        else if (tsIt->value().is_uint64())
        {
            ts = static_cast<double>(tsIt->value().as_uint64());
        }
        else
        {
            MLOG_E("SignalingEvent::fromDict key 'timestamp' expected numeric type but got type '{}' value '{}'",
                boost::json::to_string(tsIt->value().kind()),
                boost::json::serialize(tsIt->value()));
            return false;
        }

        timestamp = Poco::Timestamp(static_cast<Poco::Timestamp::TimeVal>(ts * 1000000.0));

        tags.clear();

        auto tagsIt = data.find("tags");

        if (tagsIt != data.end())
        {
            if (!tagsIt->value().is_array())
            {
                MLOG_W("SignalingEvent::fromDict optional key 'tags' expected array but got type '{}' value '{}'",
                    boost::json::to_string(tagsIt->value().kind()),
                    boost::json::serialize(tagsIt->value()));
            }
            else
            {
                const auto& arr = tagsIt->value().as_array();

                for (size_t i = 0; i < arr.size(); ++i)
                {
                    const auto& v = arr[i];

                    if (!v.is_string())
                    {
                        MLOG_W("SignalingEvent::fromDict tags[{}] expected string but got type '{}' value '{}'",
                            i,
                            boost::json::to_string(v.kind()),
                            boost::json::serialize(v));
                        continue;
                    }

                    tags.emplace_back(v.as_string().c_str());
                }
            }
        }

        MLOG_D("SignalingEvent::fromDict parsed type='{}' source='{}' tags={} payload_keys={} timestamp={}",
            type,
            source,
            tags.size(),
            payload.size(),
            ts);

        return true;
    }
    catch (const std::exception& e)
    {
        MLOG_E("SignalingEvent::fromDict exception '{}'", e.what());
        return false;
    }
} // SignalingEvent::fromDict

boost::json::object SignalingEvent::toDict() const
{
    try
    {
        boost::json::object data;

        data["type"] = "push";
        data["client_id"] = source;
        data["event_type"] = type;
        data["message"] = payload;

        boost::json::array tagsArray;

        for (const auto& tag : tags)
            tagsArray.emplace_back(tag);

        data["tags"] = std::move(tagsArray);

        const double ts = static_cast<double>(timestamp.timestamp().epochMicroseconds()) / 1000000.0;

        data["timestamp"] = ts;

        MLOG_D("SignalingEvent::toDict serialized type='{}' source='{}' tags={} payload_keys={} timestamp={}",
            type,
            source,
            tags.size(),
            payload.size(),
            ts);

        return data;
    }
    catch (const std::exception& e)
    {
        MLOG_E("SignalingEvent::toDict exception '{}'", e.what());
        return {};
    }
} // SignalingEvent::toDict()

} // namespace internal
} // namespace lunaricorn