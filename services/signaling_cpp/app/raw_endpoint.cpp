#include "raw_endpoint.h"
#include <iostream>
#include <Poco/Net/NetException.h>
#include <Poco/Timespan.h>
#include <Poco/Exception.h>

#include "stdafx.h"

namespace lunaricorn
{

static constexpr auto SERVER_HB_PERIOD = std::chrono::seconds(10);
static constexpr auto CLIENT_HB_PERIOD = std::chrono::seconds(10);

RawEndpoint::RawEndpoint(const std::string& ip, Poco::UInt16 port)
    : _serverSocket(Poco::Net::SocketAddress(Poco::Net::IPAddress(ip), port))
{
    _serverSocket.setReuseAddress(true);
    _serverSocket.setReusePort(true);
    _proto = std::make_shared<lunaricorn::internal::SignalingProto>();
}

RawEndpoint::~RawEndpoint()
{
    stop();
}

bool RawEndpoint::start()
{
    if (_stopping.load() == false) {
        _stopping = false;
        _acceptThread = std::thread(&RawEndpoint::acceptLoop, this);
        _handlerThread = std::thread(&RawEndpoint::handleClients, this);
    }
    return true;
}

bool RawEndpoint::stop()
{
    _stopping = true;

    try {
        _serverSocket.close();
    } catch (...) {}

    if (_acceptThread.joinable())
        _acceptThread.join();
    if (_handlerThread.joinable())
        _handlerThread.join();

    // Закрываем все клиентские сокеты
    std::lock_guard<std::mutex> lock(_clientsMutex);
    for (auto& [id, client] : _clients)
    {
        try 
        {
            if (!client){MBUG("[stop] no client for {}", id); continue;}
            client->sock.close(); 
        } catch (...) 
        {
            MLOG_E("cannot close {} client socket", id);
        }
    }
    _clients.clear();
    return true;
}

void RawEndpoint::acceptLoop()
{
    MLOG_D("start accept loop");
    while (!_stopping)
    {
        try
        {
            
            Poco::Net::StreamSocket clientSocket = _serverSocket.acceptConnection();
            if (_stopping) break;
            RE_Client_ptr client = std::make_shared<RE_Client>(std::move(clientSocket));
            client->sock.setBlocking(false);
            client->sock.setSendTimeout(Poco::Timespan(1, 0));
            client->update_connect_time();
            client->update_client_hb();
            client->update_server_hb();
            uint64_t id = _nextId.fetch_add(1);
            {
                std::lock_guard<std::mutex> lock(_clientsMutex);
                _clients.emplace(id, client);
            }
            MLOG_D("new client {}", id);
        }
        catch (const Poco::Exception& e)
        {
            if (!_stopping)
            {
                MLOG_E("Accept error: {}", e.displayText());
            }
            break;
        }
    }
    MLOG_D("exit accept loop");
}

void RawEndpoint::send_hb()
{
    std::lock_guard<std::mutex> lock(_clientsMutex);
    for (auto& [id, client] : _clients)
    {
        if (!client){MBUG("no client for {}", id); continue;}
        const auto hb_duration = client->server_hb_delay();
        if (hb_duration >= SERVER_HB_PERIOD)
        {
            // Send heartbeat to client
            MLOG_D("send hb to {}", id);
            sendHeartbeat(id);
            client->update_server_hb();
        }
    }
}

void RawEndpoint::handleClients()
{
    std::vector<char> buffer(4096);

    while (!_stopping)
    {
        send_hb();
        std::vector<uint64_t> clientIds;
        {
            std::lock_guard<std::mutex> lock(_clientsMutex);
            clientIds.reserve(_clients.size());
            for (const auto& [id, _] : _clients)
                clientIds.push_back(id);
        }

        for (uint64_t id : clientIds) {
            if (_stopping) break;

            Poco::Net::StreamSocket sock;
            RE_Client_ptr client;
            {
                std::lock_guard<std::mutex> lock(_clientsMutex);
                auto it = _clients.find(id);
                if (it == _clients.end())
                    continue;
                client = it->second;
            }
            if (!client)
            {
                MBUG("id {} not found", id);
                continue;
            }
            try
            {
                if (client->sock.poll(Poco::Timespan(0), Poco::Net::Socket::SELECT_READ))
                {
                    while (!_stopping) {
                        try 
                        {
                            int bytesRead = client->sock.receiveBytes(buffer.data(), static_cast<int>(buffer.size()));
                            if (bytesRead == 0) 
                            {
                                on_connectionClosed(id);
                                break;
                            }
                            processData(id, std::vector<char>(buffer.begin(), buffer.begin() + bytesRead));
                        }
                        catch (const Poco::TimeoutException&)
                        {
                            // no new data
                            break;
                        }
                        catch (const std::exception& e) 
                        {
                            MLOG_E("Error_1 processing client# {} data: {}", id, e.what());
                            on_connectionClosed(id);
                            break;
                        }
                    }
                }
            }
            catch (const std::exception& e)
            {
                MLOG_E("Error_2 processing client# {} data: {}", id, e.what());
                on_connectionClosed(id);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void RawEndpoint::on_connectionClosed(uint64_t clientId)
{
    std::lock_guard<std::mutex> lock(_clientsMutex);
    auto it = _clients.find(clientId);
    if (it != _clients.end())
    {
        auto client = it->second;
        _clients.erase(it);
        if(!client){MBUG("null client data for {}", clientId); return;}
        try { client->sock.close();} catch (...) {}
        const auto s =  std::chrono::duration_cast<std::chrono::seconds>(client->connect_time_delay()).count();
        MLOG_D("client {} closed. active {} seconds", clientId, s);
    } else {
        MBUG("unknown client id: {}", clientId);
    }
}

void RawEndpoint::processData(uint64_t clientId, const std::vector<char>& data)
{
    // Lock the buffer for this client
    std::lock_guard<std::mutex> lock(_bufferMutex);
    
    // Get or create buffer for this client
    auto& buffer = _messageBuffers[clientId];
    
    // Append new data to buffer
    buffer.buffer.insert(buffer.buffer.end(), data.begin(), data.end());
    buffer.receivedBytes += data.size();
    
    // Process complete messages from buffer
    size_t offset = 0;
    while (offset < buffer.receivedBytes) {
        if (!buffer.headerComplete) {
            // Need to read header first
            const size_t need = sizeof(lunaricorn::internal::MessageHeader) - buffer.receivedBytes;
            const size_t avail = buffer.buffer.size() - offset;
            const size_t take = std::min(need, avail);
            
            if (take > 0) {
                std::memcpy(reinterpret_cast<uint8_t*>(&buffer.header) + buffer.receivedBytes, 
                           buffer.buffer.data() + offset, take);
                buffer.receivedBytes += take;
                offset += take;
            }
            
            if (buffer.receivedBytes < sizeof(lunaricorn::internal::MessageHeader)) {
                // Need more data for header
                break;
            }
            
            buffer.headerComplete = true;
            buffer.expectedSize = sizeof(lunaricorn::internal::MessageHeader) + buffer.header.data_len;
            buffer.buffer.resize(buffer.expectedSize);
        }
        
        if (buffer.headerComplete) {
            // Check if we have the complete message
            const size_t need = buffer.expectedSize - buffer.receivedBytes;
            const size_t avail = buffer.buffer.size() - offset;
            const size_t take = std::min(need, avail);
            
            if (take > 0) {
                std::memcpy(buffer.buffer.data() + buffer.receivedBytes, 
                           buffer.buffer.data() + offset, take);
                buffer.receivedBytes += take;
                offset += take;
            }
            
            if (buffer.receivedBytes < buffer.expectedSize) {
                // Need more data for complete message
                break;
            }
            
            // Process complete message
            lunaricorn::internal::IncomingMessage msg;
            if (_proto->deserializeJson(buffer.buffer, msg)) {
                if (!msg.isValid) {
                    MLOG_E("invalid parsed message: seq={} reason={}", buffer.header.seq, msg.errorReason);
                    buffer = MessageBuffer{};  // Reset buffer on error
                    continue;
                }
                
                // Process based on message type
                switch (buffer.header.type) {
                    case lunaricorn::internal::MessageType::MT_HB:
                        processHeartbeat(clientId, msg);
                        break;
                    case lunaricorn::internal::MessageType::MT_Sub:
                        processSubscription(clientId, msg);
                        break;
                    case lunaricorn::internal::MessageType::MT_PubReq:
                        processPushRequest(clientId, msg);
                        break;
                    case lunaricorn::internal::MessageType::MT_Response:
                        processResponse(clientId, msg);
                        break;
                    case lunaricorn::internal::MessageType::MT_QueryReq:
                        processQueryRequest(clientId, msg);
                        break;
                    default:
                        processUnknownMessageType(clientId, buffer.header);
                        break;
                }
            } else {
                MLOG_E("deserializeJson failed for client {} message", clientId);
            }
            
            // Reset buffer for next message
            buffer = MessageBuffer{};
        }
    }
}

void RawEndpoint::processHeartbeat(uint64_t clientId, const lunaricorn::internal::IncomingMessage& msg)
{
    MLOG_D("Received heartbeat from client {}", clientId);
    // Update client heartbeat timestamp
    std::lock_guard<std::mutex> lock(_clientsMutex);
    auto it = _clients.find(clientId);
    if (it != _clients.end()) {
        it->second->update_client_hb();
    }
}

void RawEndpoint::processSubscription(uint64_t clientId, const lunaricorn::internal::IncomingMessage& msg)
{
    MLOG_D("Received subscription from client {}", clientId);
    // Handle subscription request
    // In a real implementation, this would register the client for specific event types
    sendResponse(clientId, msg.header.seq, true);  // Acknowledge subscription
}

void RawEndpoint::processPushRequest(uint64_t clientId, const lunaricorn::internal::IncomingMessage& msg)
{
    MLOG_D("Received push request from client {}", clientId);
    // Handle push request - forward to event system
    // In a real implementation, this would process the event and broadcast it
    
    // Send acknowledgment response
    sendResponse(clientId, msg.header.seq, true);  // Acknowledge receipt
}

void RawEndpoint::processResponse(uint64_t clientId, const lunaricorn::internal::IncomingMessage& msg)
{
    MLOG_D("Received response from client {}", clientId);
    // Handle server response (if needed)
    // In a real implementation, this might be used for handling responses to queries or pushes
}

void RawEndpoint::processQueryRequest(uint64_t clientId, const lunaricorn::internal::IncomingMessage& msg)
{
    MLOG_D("Received query request from client {}", clientId);
    // Handle query request - forward to event system
    sendResponse(clientId, msg.header.seq, true);  // Acknowledge query
}

void RawEndpoint::processUnknownMessageType(uint64_t clientId, const lunaricorn::internal::MessageHeader& header)
{
    MLOG_E("Received unknown message type {} from client {}", static_cast<int>(header.type), clientId);
}

void RawEndpoint::sendHeartbeat(uint64_t clientId)
{
    // Create heartbeat message
    lunaricorn::internal::MessageHeader hb_msg = {
        .magic = lunaricorn::internal::HeaderMagic,
        .version = lunaricorn::internal::PROTOCOL_VERSION,
        .type = lunaricorn::internal::MessageType::MT_HB,
        .data_type = lunaricorn::internal::ContentType::CT_Json,
        .flags = 0,
        .seq = 0, // no response
        .data_len = 0,
        .crc = 0
    };
    
    std::vector<uint8_t> buf;
    size_t sz = _proto->serializeJson(hb_msg, buf, boost::json::object());
    if (sz == 0) {
        MLOG_E("Failed to serialize heartbeat message");
        return;
    }
    
    // Send to client
    std::lock_guard<std::mutex> lock(_clientsMutex);
    auto it = _clients.find(clientId);
    if (it != _clients.end()) {
        try {
            _proto->send_raw(std::make_shared<Poco::Net::StreamSocket>(it->second->sock), buf);
        } catch (const std::exception& e) {
            MLOG_E("Failed to send heartbeat to client {}: {}", clientId, e.what());
        }
    }
}

void RawEndpoint::sendResponse(uint64_t clientId, uint64_t seq, bool success, const boost::json::object& data)
{
    // Create response message
    lunaricorn::internal::MessageHeader resp_msg = {
        .magic = lunaricorn::internal::HeaderMagic,
        .version = lunaricorn::internal::PROTOCOL_VERSION,
        .type = lunaricorn::internal::MessageType::MT_Response,
        .data_type = lunaricorn::internal::ContentType::CT_Json,
        .flags = 0,
        .seq = seq,
        .data_len = 0,
        .crc = 0
    };
    
    std::vector<uint8_t> buf;
    size_t sz = _proto->serializeJson(resp_msg, buf, data);
    if (sz == 0) {
        MLOG_E("Failed to serialize response message");
        return;
    }
    
    // Send to client
    std::lock_guard<std::mutex> lock(_clientsMutex);
    auto it = _clients.find(clientId);
    if (it != _clients.end()) {
        try {
            _proto->send_raw(std::make_shared<Poco::Net::StreamSocket>(it->second->sock), buf);
        } catch (const std::exception& e) {
            MLOG_E("Failed to send response to client {}: {}", clientId, e.what());
        }
    }
}

void RawEndpoint::handleEvent(const EventData& event)
{
    // Forward events to subscribed clients
    // This is a stub implementation - in a real system this would be more complex
    MLOG_D("Handling event: {}", event.type);
}

} // namespace lunaricorn
