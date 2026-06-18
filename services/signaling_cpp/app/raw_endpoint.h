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
#include "raw_endpoint_client.h"
#include "proto/signaling.h"
#include <chrono>
#include <queue>

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

    // Buffer for accumulating protocol messages
    struct MessageBuffer {
        std::vector<char> buffer;
        size_t receivedBytes = 0;
        bool headerComplete = false;
        lunaricorn::internal::MessageHeader header {};
        size_t expectedSize = 0;
    };

    // Message processing functions for different message types
    void processHeartbeat(uint64_t clientId, const lunaricorn::internal::IncomingMessage& msg);
    void processSubscription(uint64_t clientId, const lunaricorn::internal::IncomingMessage& msg);
    void processPushRequest(uint64_t clientId, const lunaricorn::internal::IncomingMessage& msg);
    void processResponse(uint64_t clientId, const lunaricorn::internal::IncomingMessage& msg);
    void processQueryRequest(uint64_t clientId, const lunaricorn::internal::IncomingMessage& msg);
    void processUnknownMessageType(uint64_t clientId, const lunaricorn::internal::MessageHeader& header);

    // Send response methods
    void sendHeartbeat(uint64_t clientId);
    void sendResponse(uint64_t clientId, uint64_t seq, bool success, const boost::json::object& data = {});

    Poco::Net::ServerSocket _serverSocket;
    std::map<uint64_t, RE_Client_ptr> _clients;
    std::atomic<uint64_t> _nextId { 1 };
    std::atomic<bool> _stopping { false };

    std::mutex _clientsMutex;
    std::thread _acceptThread;
    std::thread _handlerThread;

    // Protocol handling
    std::shared_ptr<lunaricorn::internal::SignalingProto> _proto;

    // Message buffer for each client
    std::map<uint64_t, MessageBuffer> _messageBuffers;
    std::mutex _bufferMutex;
}; // class RawEndpoint
} // namespace lunaricorn