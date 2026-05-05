#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>

#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Net/SocketAddress.h>

#include "endpoint.h"
#include <chrono>
namespace lunaricorn
{

class RawEndpoint : public Endpoint
{
public:
    RawEndpoint(const std::string& ip, Poco::UInt16 port);
    ~RawEndpoint();

    virtual bool start()  override;
    virtual bool stop()  override;
    virtual void handleEvent(const EventData& event) override;

private:
    struct Client_
    {
        using Clock = std::chrono::steady_clock;
        Poco::Net::StreamSocket sock;
        std::chrono::time_point<Clock> connectTime;
        std::chrono::time_point<Clock> client_hb;
        std::chrono::time_point<Clock> server_hb;
        auto connect_time_delay() const { return Clock::now() - connectTime; }
        auto client_hb_delay() const { return Clock::now() - client_hb; }
        auto server_hb_delay() const { return Clock::now() - server_hb; }
        void update_connect_time() { connectTime = Clock::now(); }
        void update_client_hb() { client_hb = Clock::now(); }
        void update_server_hb() { server_hb = Clock::now(); }
    };
    using PClient = std::shared_ptr<Client_>;
    void acceptLoop();
    void handleClients();
    void processData(uint64_t clientId, const std::vector<char>& data);
    void on_connectionClosed(uint64_t clientId);
    void send_hb();


    Poco::Net::ServerSocket _serverSocket;
    std::map<uint64_t, PClient> _clients;
    std::atomic<uint64_t> _nextId { 1 };
    std::atomic<bool> _stopping { false };

    std::mutex _clientsMutex;
    std::thread _acceptThread;
    std::thread _handlerThread;
}; // class RawEndpoint
} // namespace lunaricorn
