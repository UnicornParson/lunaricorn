#include "signaling_api.h"
#include <chrono>
#include <random>

using namespace lunaricorn::internal;
using namespace std::chrono_literals; 

namespace lunaricorn
{

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
    MBUG("'events' shoud be an event object or events array");
    return false;
} // SignalingSubEvent::build


void SignalingPushRequest::make_header(lunaricorn::internal::MessageHeader& header) const
{
    header.magic = HeaderMagic;
    header.version = PROTOCOL_VERSION;
    header.type = MessageType::MT_PubReq;
    header.data_type = CT_Json;
    header.flags = 0;
    header.seq = _seq;
} // SignalingPushRequest::make_header

} // namespace lunaricorn