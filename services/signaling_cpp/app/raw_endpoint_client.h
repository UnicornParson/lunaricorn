#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <chrono>

#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Net/SocketAddress.h>

#include "endpoint.h"
#include "raw_endpoint_client.h"
namespace lunaricorn
{

class RE_Client
{
public:
    using Clock = std::chrono::steady_clock;
    explicit RE_Client(Poco::Net::StreamSocket&& socket)
    ~RE_Client();
    static inline int64_t clients_count() {return _count.load(std::memory_order_relaxed); }

    inline auto connect_time_delay() const { return Clock::now() - connectTime; }
    inline auto client_hb_delay() const { return Clock::now() - _client_hb; }
    inline auto server_hb_delay() const { return Clock::now() - _server_hb; }
    inline void update_connect_time() { _connectTime = Clock::now(); }
    inline void update_client_hb() { _client_hb = Clock::now(); }
    inline void update_server_hb() { _server_hb = Clock::now(); }

    void processData(uint64_t clientId, const std::vector<char>& data);

    Poco::Net::StreamSocket sock;


private:
    
    std::chrono::time_point<Clock> _connectTime;
    std::chrono::time_point<Clock> _client_hb;
    std::chrono::time_point<Clock> _server_hb;

    inline static std::atomic<int64_t> _count = 0;
}; // class RE_Client
using RE_Client_ptr = std::shared_ptr<RE_Client>;

} // namespace lunaricorn