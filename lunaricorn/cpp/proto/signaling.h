#pragma once
#include <cstdint>
#include <atomic>
#include <vector>
#include <boost/json.hpp>

namespace lunaricorn {
namespace internal {

static constexpr uint8_t PH = 0xAB;
static constexpr uint32_t HeaderMagic = 0x12345678;
static constexpr uint8_t  PROTOCOL_VERSION = 1;
static constexpr uint32_t MAX_DATA_LEN = 128 * 1024 * 1024; // 128kb

enum MessageType : uint8_t {
    MT_Invalid   = 0,
    MT_HB        = 1,
    MT_Response  = 2,
    MT_PubReq    = 3,
    MT_QueryReq  = 4
};

enum ContentType : uint8_t {
    CT_Raw  = 0,
    CT_Json = 1
};

#pragma pack(push, 1)
struct MessageHeader 
{
    uint32_t magic = HeaderMagic;
    uint8_t version = PROTOCOL_VERSION;
    MessageType type = MT_Invalid;
    ContentType data_type = CT_Raw;
    uint8_t  flags; 
    uint32_t data_len;
    uint32_t crc;
};
#pragma pack(pop)

struct IncomingMessage
{
    MessageHeader header;
    boost::json::object data;
    bool isValid = false;
    std::string errorReason;
};

class SignalingProto
{
public:
    struct Stats
    {
        std::atomic<uint64_t> ok = 0;
        std::atomic<uint64_t> fails = 0;
    };
    const inline SignalingProto::Stats& stats() const { return s_; }
    bool deserializeJson(const std::vector<uint8_t>& buf, IncomingMessage& out);
    size_t serializeJson(MessageHeader& msg, std::vector<uint8_t>& buf, const boost::json::object& data);
private:
    SignalingProto::Stats s_;
}; // class SignalingProto
} // namespace internal
} // namespace lunaricorn
