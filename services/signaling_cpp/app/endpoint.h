#pragma once


#include <lunaricorn.h>
#include <event_data.h>

namespace lunaricorn
{
using Counter = std::atomic<uint64_t>;
struct EndpointStats
{
    Counter requests { 0 };
    Counter errors { 0 };

}; // struct EndpointStats
class Endpoint
{
public:
    virtual void handleEvent(const EventData& event) = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;

    const EndpointStats& stats() { return _stats; }
protected:
    EndpointStats _stats;
}; // class Endpoint

}