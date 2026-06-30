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

RawEndpoint::RawEndpoint(const std::string& ip, Poco::UInt16 port, SignalingEnginePtr engine)
    : _serverSocket(Poco::Net::SocketAddress(Poco::Net::IPAddress(ip), port)),
    _engine(engine)
{
    if (!_engine)
    {
        throw std::runtime_error("no engine");
    }
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
            client->socket().close(); 
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
            MLOG_D("acceptLoop: waiting for incoming connection...");
            Poco::Net::StreamSocket clientSocket = _serverSocket.acceptConnection();
            if (_stopping) break;

            // Get client address for logging
            const Poco::Net::SocketAddress& addr = clientSocket.address();
            MLOG_D("acceptLoop: new connection from {}:{}, port {}", 
                   addr.host().toString(), addr.host().toString(), addr.port());

            // Create client and transfer socket ownership
            RE_Client_ptr client = std::make_shared<RE_Client>(std::move(clientSocket));
            client->socket().setBlocking(false);
            client->socket().setSendTimeout(Poco::Timespan(1, 0));
            client->update_client_hb();
            client->update_server_hb();

            uint64_t id = _nextId.fetch_add(1);
            client->set_id(id);

            MLOG_D("acceptLoop: assigned client id={}, total clients={}", id, RE_Client::clients_count());

            // Set up callback for server-side message processing
            client->set_message_callback([this](uint64_t clientId, const lunaricorn::internal::IncomingMessage& msg) {
                on_client_message(clientId, msg);
            });

            {
                std::lock_guard<std::mutex> lock(_clientsMutex);
                _clients.emplace(id, client);
                MLOG_D("acceptLoop: client {} added to _clients map, total connected: {}", 
                       id, _clients.size());
            }
            MLOG_D("acceptLoop: new client {} accepted and registered", id);
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
                if (client->socket().poll(Poco::Timespan(0), Poco::Net::Socket::SELECT_READ))
                {
                    while (!_stopping) {
                        try 
                        {
                            int bytesRead = client->socket().receiveBytes(buffer.data(), static_cast<int>(buffer.size()));
                            if (bytesRead == 0) 
                            {
                                on_client_closed(id);
                                break;
                            }
                            // Pass data to client for accumulation and parsing
                            client->processData(std::vector<char>(buffer.begin(), buffer.begin() + bytesRead));
                        }
                        catch (const Poco::TimeoutException&)
                        {
                            // no new data
                            break;
                        }
                        catch (const std::exception& e) 
                        {
                            MLOG_E("Error_1 processing client# {} data: {}", id, e.what());
                            on_client_closed(id);
                            break;
                        }
                    }
                }
            }
            catch (const std::exception& e)
            {
                MLOG_E("Error_2 processing client# {} data: {}", id, e.what());
                on_client_closed(id);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void RawEndpoint::on_client_message(uint64_t clientId, const lunaricorn::internal::IncomingMessage& msg)
{
    MLOG_D("on_client_message[{}]: received msg type={} data_type={} seq={} data_len={}", 
           clientId, static_cast<int>(msg.header.type), 
           static_cast<int>(msg.header.data_type), msg.header.seq, msg.header.data_len);

    // Process based on message type
    switch (msg.header.type) {
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
            processUnknownMessageType(clientId, msg.header);
            break;
    }
}

void RawEndpoint::on_client_closed(uint64_t clientId)
{
    MLOG_D("on_client_closed[{}]: client closing, current connected: {}", 
           clientId, _clients.size());

    std::lock_guard<std::mutex> lock(_clientsMutex);
    auto it = _clients.find(clientId);
    if (it != _clients.end())
    {
        auto client = it->second;
        _clients.erase(it);
        if(!client){MBUG("null client data for {}", clientId); return;}
        try { client->socket().close();} catch (...) {}
        const auto s = std::chrono::duration_cast<std::chrono::seconds>(client->client_hb_delay()).count();
        MLOG_D("on_client_closed[{}]: client disconnected. session_duration={}s, total connected: {}", 
               clientId, s, _clients.size());
    } else {
        MLOG_D("on_client_closed[{}]: client not found in map (already removed)", clientId);
    }
}

void RawEndpoint::processHeartbeat(uint64_t clientId, const lunaricorn::internal::IncomingMessage& msg)
{
    MLOG_D("processHeartbeat[{}]: received heartbeat, data_len={}", clientId, msg.header.data_len);
    // Update client heartbeat timestamp
    std::lock_guard<std::mutex> lock(_clientsMutex);
    auto it = _clients.find(clientId);
    if (it != _clients.end()) {
        it->second->update_client_hb();
        MLOG_D("processHeartbeat[{}]: heartbeat updated, total connected: {}", clientId, _clients.size());
    } else {
        MBUG("processHeartbeat[{}]: client not found in _clients map", clientId);
    }
}

void RawEndpoint::processSubscription(uint64_t clientId, const lunaricorn::internal::IncomingMessage& msg)
{
    MLOG_D("processSubscription[{}]: received subscription request, seq={}, data_len={}", 
           clientId, msg.header.seq, msg.header.data_len);
    
    // Log subscription data if available
    if (msg.header.data_len > 0 && !msg.data.empty()) {
        MLOG_D("processSubscription[{}]: subscription payload size={}", 
               clientId, msg.data.size());
    }

    // Handle subscription request
    // In a real implementation, this would register the client for specific event types
    MLOG_D("processSubscription[{}]: sending acknowledgment", clientId);
    sendResponse(clientId, msg.header.seq, true);  // Acknowledge subscription
    MLOG_D("processSubscription[{}]: subscription acknowledged", clientId);
}

void RawEndpoint::processPushRequest(uint64_t clientId, const lunaricorn::internal::IncomingMessage& msg)
{
    MLOG_D("processPushRequest[{}]: received push request, seq={}, data_len={}", 
           clientId, msg.header.seq, msg.header.data_len);
    
    // Log push data if available
    if (msg.header.data_len > 0 && !msg.data.empty()) {
        MLOG_D("processPushRequest[{}]: push payload size={}", 
               clientId, msg.data.size());
    }

    // Handle push request - forward to event system
    // In a real implementation, this would process the event and broadcast it
    
    // Send acknowledgment response
    MLOG_D("processPushRequest[{}]: sending acknowledgment", clientId);
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
    MLOG_D("processQueryRequest[{}]: received query request, seq={}, data_len={}", 
           clientId, msg.header.seq, msg.header.data_len);
    
    // Log query data if available
    if (msg.header.data_len > 0 && !msg.data.empty()) {
        MLOG_D("processQueryRequest[{}]: query payload size={}", 
               clientId, msg.data.size());
    }

    // Handle query request - forward to event system
    MLOG_D("processQueryRequest[{}]: sending acknowledgment", clientId);
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
            MLOG_D("sendHeartbeat[{}]: sending heartbeat", clientId);
            it->second->send_message(hb_msg, boost::json::object());
        } catch (const std::exception& e) {
            MLOG_E("Failed to send heartbeat to client {}: {}", clientId, e.what());
        }
    } else {
        MBUG("sendHeartbeat[{}]: client not found", clientId);
    }
}

void RawEndpoint::sendResponse(uint64_t clientId, uint64_t seq, bool success, const boost::json::object& data)
{
    MLOG_D("sendResponse[{}]: sending response, seq={}, success={}, data_obj_size={}", 
           clientId, seq, success, data.size());
    
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
        MLOG_E("sendResponse[{}]: Failed to serialize response message", clientId);
        return;
    }
    
    // Send to client
    std::lock_guard<std::mutex> lock(_clientsMutex);
    auto it = _clients.find(clientId);
    if (it != _clients.end()) {
        try {
            it->second->send_message(resp_msg, data);
            MLOG_D("sendResponse[{}]: response sent successfully", clientId);
        } catch (const std::exception& e) {
            MLOG_E("sendResponse[{}]: Failed to send response to client {}: {}", 
                   clientId, clientId, e.what());
        }
    } else {
        MBUG("sendResponse[{}]: client not found in _clients map", clientId);
    }
}

void RawEndpoint::handleEvent(const EventData& event)
{
    // Forward events to subscribed clients
    // This is a stub implementation - in a real system this would be more complex
    MLOG_D("Handling event: {}", event.event_type);
}

} // namespace lunaricorn