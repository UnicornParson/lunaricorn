#pragma once
#include "stdafx.h"
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

    void acceptLoop();
    void handleClients();
    void processData(uint64_t clientId, const std::vector<char>& data);
    void on_connectionClosed(uint64_t clientId);
    void send_hb();


    Poco::Net::ServerSocket _serverSocket;
    std::map<uint64_t, RE_Client_ptr> _clients;
    std::atomic<uint64_t> _nextId { 1 };
    std::atomic<bool> _stopping { false };

    std::mutex _clientsMutex;
    std::thread _acceptThread;
    std::thread _handlerThread;
}; // class RawEndpoint
} // namespace lunaricorn
