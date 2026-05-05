#include "signaling.h"
#include <vector>
#include <cstdint>
#include <string>
#include <cstring>
#include <format>
#include <boost/json.hpp>
#include <boost/crc.hpp>

namespace lunaricorn {
namespace internal {

size_t SignalingProto::serializeJson(MessageHeader& msg, std::vector<uint8_t>& buf, const boost::json::object& data)
{
    MessageHeader hdr = msg;
    hdr.data_type = CT_Json;

    std::string json_str = boost::json::serialize(data);
    size_t json_len = json_str.size();

    if (json_len > MAX_DATA_LEN) {
        throw std::length_error("Serialized JSON size exceeds MAX_DATA_LEN");
    }

    boost::crc_32_type crc_calculator;
    crc_calculator.process_bytes(json_str.data(), json_len);
    hdr.crc = crc_calculator.checksum();
    hdr.data_len = static_cast<uint32_t>(json_len);

    const uint8_t* hdr_ptr = reinterpret_cast<const uint8_t*>(&hdr);
    buf.insert(buf.end(), hdr_ptr, hdr_ptr + sizeof(hdr));

    buf.insert(buf.end(), json_str.begin(), json_str.end());

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





} // namespace internal
} // namespace lunaricorn