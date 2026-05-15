#pragma once
#include <cstdint>
#include <atomic>
#include <vector>
#include <list>
#include <boost/json.hpp>
#include <Poco/DateTime.h>
namespace lunaricorn {
namespace internal {

static constexpr uint8_t PH = 0xAB;
static constexpr uint32_t HeaderMagic = 0x12345678;
static constexpr uint8_t  PROTOCOL_VERSION = 1;
static constexpr uint32_t MAX_DATA_LEN = 128 * 1024 * 1024; // 128kb

using StringList = std::list<std::string>;

enum MessageType : uint8_t 
{
    MT_Invalid   = 0,
    MT_HB        = 1,
    MT_Response  = 2,
    MT_PubReq    = 3,
    MT_QueryReq  = 4
};

enum ContentType : uint8_t 
{
    CT_Raw  = 0,
    CT_Json = 1
};

namespace SignalingEventType
{
    static const std::string System = "sys";
    static const std::string Broadcast = "broadcast";
    static const std::string Robots = "robots";
    static const std::string FileOp_new = "FileOp_new";
    static const std::string FileOp_update = "FileOp_update";
    static const std::string FileOp_delete = "FileOp_delete";
    static const std::string FileOp_notify = "FileOp_notify";
}
namespace SignalingEventTags
{
    static const std::string JobEntry = "JobEntry";
    static const std::string JobExit = "JobExit";
    static const std::string News = "News";
    static const std::string Md = "Md";
    static const std::string Obsidian = "Obsidian";
}

//    def push_event(self, event_type: str, payload: Dict[str, Any], 
//source: Optional[str] = None, tags: Optional[List] = None) -> Dict[str, Any]:

struct SignalingEvent
{
    std::string type;
    StringList tags;
    std::string source;
    boost::json::object payload;
    Poco::DateTime timestamp;
    bool fromDict(const boost::json::object& data);
    boost::json::object toDict() const;
};


#pragma pack(push, 1)
struct MessageHeader 
{
    uint32_t magic = HeaderMagic;
    uint64_t seq = 0;
    // 32b pack ---
    uint8_t version = PROTOCOL_VERSION;
    MessageType type = MT_Invalid;
    ContentType data_type = CT_Raw;
    uint8_t  flags; 
    // ---

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
    bool send_raw(std::shared_ptr<Poco::Net::StreamSocket> sock, const std::vector<uint8_t>& data);
private:
    SignalingProto::Stats s_;

}; // class SignalingProto
} // namespace internal
} // namespace lunaricorn
