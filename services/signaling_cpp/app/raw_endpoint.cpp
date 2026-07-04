#include "raw_endpoint.h"
#include "telemetry.h"
#include <iostream>
#include <Poco/Net/NetException.h>
#include <Poco/Timespan.h>
#include <Poco/Exception.h>
#include <cerrno>
#include <boost/json.hpp>

#include "stdafx.h"

namespace lunaricorn
{

static constexpr auto SERVER_HB_PERIOD = std::chrono::seconds(10);
static constexpr auto CLIENT_HB_PERIOD = std::chrono::seconds(10);

// ---- Helper: parse subscription JSON payload ----
static bool parseSubscriptionPayload(
    const boost::json::value& jv,
    std::vector<std::string>& types,
    std::vector<std::string>& sources,
    std::vector<std::string>& affected,
    std::vector<std::string>& tags)
{
    if (!jv.is_object()) {
        MLOG_E("parseSubscriptionPayload: payload is not a JSON object");
        return false;
    }

    const auto& obj = jv.as_object();

    auto extract_array = [](const boost::json::value& val) -> std::vector<std::string> {
        std::vector<std::string> result;
        if (val.is_array()) {
            for (const auto& elem : val.as_array()) {
                if (elem.is_string()) {
                    result.push_back(elem.as_string().c_str());
                }
            }
        }
        return result;
    };

    if (obj.contains("types")) {
        types = extract_array(obj.at("types"));
    }
    if (obj.contains("sources")) {
        sources = extract_array(obj.at("sources"));
    }
    if (obj.contains("affected")) {
        affected = extract_array(obj.at("affected"));
    }
    if (obj.contains("tags")) {
        tags = extract_array(obj.at("tags"));
    }

    return true;
}

// ---- Helper: extract a string field from a JSON object ----
static std::string extractJsonStringField(const boost::json::value& jv, const std::string& field, const std::string& default_val)
{
    if (jv.is_object() && jv.as_object().contains(field)) {
        const auto& val = jv.as_object().at(field);
        if (val.is_string()) {
            return val.as_string().c_str();
        }
    }
    return default_val;
}

// ---- Helper: extract an optional string field from JSON ----
static std::optional<std::string> extractOptionalJsonString(const boost::json::value& jv, const std::string& field)
{
    if (jv.is_object() && jv.as_object().contains(field)) {
        const auto& val = jv.as_object().at(field);
        if (val.is_string()) {
            return std::string(val.as_string().c_str());
        }
    }
    return std::nullopt;
}

// ---- Helper: extract json::value field ----
static boost::json::value extractJsonValueField(const boost::json::value& jv, const std::string& field)
{
    if (jv.is_object() && jv.as_object().contains(field)) {
        return jv.as_object().at(field);
    }
    return boost::json::value();
}

// ---- Helper: extract tags array from JSON ----
static std::vector<std::string> extractJsonTagsField(const boost::json::value& jv)
{
    if (jv.is_object() && jv.as_object().contains("tags")) {
        const auto& tags_val = jv.as_object().at("tags");
        if (tags_val.is_array()) {
            std::vector<std::string> result;
            for (const auto& elem : tags_val.as_array()) {
                if (elem.is_string()) {
                    result.push_back(elem.as_string().c_str());
                }
            }
            return result;
        }
    }
    return {};
}

// ---- Helper: get current timestamp in seconds since epoch ----
static double getCurrentTimestamp()
{
    return std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

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
    Telemetry::instance().setActiveClients(0);
}

RawEndpoint::~RawEndpoint()
{
    stop();
}

bool RawEndpoint::start()
{
    if (_stopping.load()) {
        _stopping = false;
    }
    if (!_acceptThread.joinable() && !_handlerThread.joinable()) {
        _acceptThread = std::thread(&RawEndpoint::acceptLoop, this);
        _handlerThread = std::thread(&RawEndpoint::handleClients, this);
    }
    return true;
}

bool RawEndpoint::stop()
{
    _stopping = true;

    // Step 1: Close server socket to interrupt acceptLoop()
    try {
        _serverSocket.close();
    } catch (...) {}

    // Step 2: Snapshot clients and close sockets without holding the lock.
    // This avoids a deadlock: handleClients() may be in receiveBytes() which
    // throws after socket close, then calls on_client_closed() which needs
    // _clientsMutex. If stop() holds _clientsMutex during join(), deadlock.
    std::vector<RE_Client_ptr> clients_snapshot;
    {
        std::lock_guard<std::mutex> lock(_clientsMutex);
        clients_snapshot.reserve(_clients.size());
        for (auto& [id, client] : _clients)
        {
            if (client) clients_snapshot.push_back(client);
        }
    }
    for (auto& client : clients_snapshot)
    {
        try {
            client->socket().close();
        } catch (...) {
            // Ignore, socket may already be closed
        }
    }

    // Step 3: Wait for both threads to finish
    if (_acceptThread.joinable())
        _acceptThread.join();
    if (_handlerThread.joinable())
        _handlerThread.join();

    // Step 4: Clean up clients
    {
        std::lock_guard<std::mutex> lock(_clientsMutex);
        _clients.clear();
    }
    return true;
}

void RawEndpoint::acceptLoop()
{
    MLOG_D("start accept loop");
    while (!_stopping)
    {
        try
        {
            // Use poll with timeout to allow timely exit when _stopping is set
            if (!_serverSocket.poll(Poco::Timespan(1, 0), Poco::Net::Socket::SELECT_READ))
            {
                continue;
            }
            if (_stopping) break;

            MLOG_D("acceptLoop: waiting for incoming connection...");
            Poco::Net::StreamSocket clientSocket = _serverSocket.acceptConnection();
            if (_stopping) break;

            // Get client address for logging
            const Poco::Net::SocketAddress& addr = clientSocket.address();
            MLOG_D("acceptLoop: new connection from {}:{}, port {}", 
                   addr.host().toString(), addr.host().toString(), addr.port());

            // Create client and transfer socket ownership
            RE_Client_ptr client = std::make_shared<RE_Client>(std::move(clientSocket), _engine);
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
                Telemetry::instance().setActiveClients(_clients.size());
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
    // Collect client IDs that need heartbeat while holding the lock,
    // then send heartbeats without holding the lock to avoid
    // deadlock with sendHeartbeat() which also locks _clientsMutex.
    std::vector<uint64_t> hb_targets;
    {
        std::lock_guard<std::mutex> lock(_clientsMutex);
        for (auto& [id, client] : _clients)
        {
            if (!client) { MBUG("no client for {}", id); continue; }
            const auto hb_duration = client->server_hb_delay();
            if (hb_duration >= SERVER_HB_PERIOD)
            {
                hb_targets.push_back(id);
                client->update_server_hb();
            }
        }
    }

    // Send heartbeats outside the lock
    for (uint64_t id : hb_targets)
    {
        MLOG_D("send hb to {}", id);
        sendHeartbeat(id);
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
        // Update active telemetry count for each iteration
        Telemetry::instance().setActiveClients(clientIds.size());
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
                            if (bytesRead > 0)
                            {
                                // Pass data to client for accumulation and parsing
                                client->processData(std::vector<char>(buffer.begin(), buffer.begin() + bytesRead));
                            }
                            else if (bytesRead == 0)
                            {
                                // Connection closed by peer
                                on_client_closed(id);
                                break;
                            }
                            else
                            {
                                // bytesRead < 0: non-blocking read returned error
                                // In modern POCO, check errno directly for EAGAIN/EWOULDBLOCK
#ifdef EAGAIN
                                if (errno == EAGAIN)
#endif
                                {
                                    // No data available, skip to next client
                                    break;
                                }
#ifdef EWOULDBLOCK
                                if (errno == EWOULDBLOCK)
#endif
                                {
                                    // No data available, skip to next client
                                    break;
                                }
                                MLOG_E("receiveBytes failed for client#{}: error code={}", id, bytesRead);
                                on_client_closed(id);
                                break;
                            }
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

        {
            std::lock_guard<std::mutex> lock(_clientsMutex);
            auto it = _clients.find(clientId);
            if (it != _clients.end())
            {
                auto client = it->second;
                _clients.erase(it);
                if(!client){MBUG("null client data for {}", clientId); return;}
                try { client->socket().close();} catch (...) {}
                Telemetry::instance().setActiveClients(_clients.size());
                const auto s = std::chrono::duration_cast<std::chrono::seconds>(client->client_hb_delay()).count();
                MLOG_D("on_client_closed[{}]: client disconnected. session_duration={}s, total connected: {}", 
                       clientId, s, _clients.size());
            } else {
                MLOG_D("on_client_closed[{}]: client not found in map (already removed)", clientId);
            }
        }

    // Auto-unsubscribe from events when client disconnects
    if (_engine) {
        _engine->unsubscribe(clientId);
        MLOG_D("on_client_closed[{}]: auto-unsubscribed from events", clientId);
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
    
    std::vector<std::string> types, sources, affected, tags;
    
    try {
        boost::json::value jv = boost::json::value(msg.data);
        if (!parseSubscriptionPayload(jv, types, sources, affected, tags)) {
            MLOG_E("processSubscription[{}]: invalid subscription payload", clientId);
            sendResponse(clientId, msg.header.seq, false, 
                {{"error", boost::json::value("invalid subscription payload")}});
            return;
        }
        MLOG_D("processSubscription[{}]: parsed filters: types={} sources={} affected={} tags={}", 
               clientId, types.size(), sources.size(), affected.size(), tags.size());
        
        // Subscribe client in the engine
        _engine->subscribe(clientId, types, sources, affected, tags);
        
        MLOG_D("processSubscription[{}]: subscribed with {} filters", clientId, 
               types.size() + sources.size() + affected.size() + tags.size());
        
        sendResponse(clientId, msg.header.seq, true, 
            {{"subscribed", true}, {"client_id", (uint64_t)clientId}});
    } catch (const std::exception& e) {
        MLOG_E("processSubscription[{}]: error: {}", clientId, e.what());
        sendResponse(clientId, msg.header.seq, false, 
            {{"error", boost::json::value(e.what())}});
        return;
    }
}

void RawEndpoint::processPushRequest(uint64_t clientId, const lunaricorn::internal::IncomingMessage& msg)
{
    MLOG_D("processPushRequest[{}]: received push request, seq={}, data_len={}", 
           clientId, msg.header.seq, msg.header.data_len);
    
    if (msg.header.data_len > 0) {
        try {
            // Convert incoming message to StoredEventData
            boost::json::value jv = boost::json::value(msg.data);
            StoredEventData event_data;
            event_data.event_type = extractJsonStringField(jv, "type", "unknown");
            event_data.source = extractOptionalJsonString(jv, "source");
            event_data.affected = extractJsonValueField(jv, "affected");
            if (!event_data.affected.is_array()) {
                event_data.affected = boost::json::array();
            }
            event_data.tags = extractJsonTagsField(jv);
            event_data.payload = extractJsonValueField(jv, "payload");
            if (event_data.payload.is_null()) {
                // Use the whole data as payload if no "payload" key
                event_data.payload = jv;
            }
            event_data.timestamp = getCurrentTimestamp();
            
            // Create event in storage and broadcast to subscribers
            long long eid = _engine->createEvent(event_data);
            _engine->dispatchEvent(event_data);
            
            Telemetry::instance().recordPushSuccess();
            MLOG_D("processPushRequest[{}]: event created id={} type={} broadcast to subscribers", 
                   clientId, eid, event_data.event_type);
            
            sendResponse(clientId, msg.header.seq, true, 
                {{"event_id", (long long)eid}, {"published", true}});
        } catch (const std::exception& e) {
            Telemetry::instance().recordError();
            MLOG_E("processPushRequest[{}]: failed: {}", clientId, e.what());
            sendResponse(clientId, msg.header.seq, false, 
                {{"error", boost::json::value(e.what())}});
        }
    } else {
        MLOG_W("processPushRequest[{}]: empty payload", clientId);
        sendResponse(clientId, msg.header.seq, false, 
            {{"error", boost::json::value("empty payload")}});
    }
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
    
    if (msg.header.data_len == 0 || msg.data.empty()) {
        MLOG_W("processQueryRequest[{}]: empty query payload", clientId);
        sendResponse(clientId, msg.header.seq, false,
            {{"error", boost::json::value("empty query payload")}});
        return;
    }

    try {
        boost::json::value jv = boost::json::value(msg.data);
        if (!jv.is_object()) {
            MLOG_E("processQueryRequest[{}]: query payload is not a JSON object", clientId);
            sendResponse(clientId, msg.header.seq, false,
                {{"error", boost::json::value("query payload must be a JSON object")}});
            return;
        }

        const auto& obj = jv.as_object();

        // Parse optional timestamp filter (default: 0 = from beginning of time)
        double timestamp = 0.0;
        auto ts_it = obj.find("timestamp");
        if (ts_it != obj.end() && ts_it->value().is_double()) {
            timestamp = ts_it->value().as_double();
        } else if (ts_it != obj.end() && ts_it->value().is_int64()) {
            timestamp = static_cast<double>(ts_it->value().as_int64());
        }

        // Parse optional 'types' filter
        std::vector<std::string> types;
        auto types_it = obj.find("types");
        if (types_it != obj.end() && types_it->value().is_array()) {
            for (const auto& elem : types_it->value().as_array()) {
                if (elem.is_string()) {
                    types.push_back(elem.as_string().c_str());
                }
            }
        }

        // Parse optional 'sources' filter
        std::vector<std::string> sources;
        auto sources_it = obj.find("sources");
        if (sources_it != obj.end() && sources_it->value().is_array()) {
            for (const auto& elem : sources_it->value().as_array()) {
                if (elem.is_string()) {
                    sources.push_back(elem.as_string().c_str());
                }
            }
        }

        // Parse optional 'affected' filter
        std::vector<std::string> affected;
        auto affected_it = obj.find("affected");
        if (affected_it != obj.end() && affected_it->value().is_array()) {
            for (const auto& elem : affected_it->value().as_array()) {
                if (elem.is_string()) {
                    affected.push_back(elem.as_string().c_str());
                }
            }
        }

        // Parse optional 'tags' filter
        std::vector<std::string> tags;
        auto tags_it = obj.find("tags");
        if (tags_it != obj.end() && tags_it->value().is_array()) {
            for (const auto& elem : tags_it->value().as_array()) {
                if (elem.is_string()) {
                    tags.push_back(elem.as_string().c_str());
                }
            }
        }

        // Parse optional limit
        int limit = 0;
        auto limit_it = obj.find("limit");
        if (limit_it != obj.end() && limit_it->value().is_int64()) {
            limit = static_cast<int>(limit_it->value().as_int64());
        }

        MLOG_D("processQueryRequest[{}]: query params: timestamp={}, types={}, sources={}, affected={}, tags={}, limit={}",
               clientId, timestamp, types.size(), sources.size(), affected.size(), tags.size(), limit);

        // Execute search through the engine
        auto results = _engine->findEvents(timestamp, types, sources, affected, tags, limit);

        MLOG_D("processQueryRequest[{}]: found {} events", clientId, results.size());

        // Build response payload
        boost::json::array events_arr;
        for (const auto& event : results) {
            boost::json::object ev;
            ev["eid"] = boost::json::value(static_cast<int64_t>(event.eid));
            ev["type"] = boost::json::value(event.event_type);
            if (event.source.has_value()) {
                ev["source"] = boost::json::value(*event.source);
            }
            ev["timestamp"] = boost::json::value(event.timestamp);
            ev["payload"] = event.payload;
            if (event.affected.is_array() && !event.affected.as_array().empty()) {
                ev["affected"] = event.affected;
            }
            // Add tags
            boost::json::array tags_arr;
            for (const auto& tag : event.tags) {
                tags_arr.push_back(boost::json::value(tag));
            }
            ev["tags"] = std::move(tags_arr);
            events_arr.push_back(std::move(ev));
        }

        Telemetry::instance().recordPushSuccess(); // reuse counter for successful queries
        sendResponse(clientId, msg.header.seq, true,
            {{"events", std::move(events_arr)}, {"count", static_cast<int64_t>(results.size())}});

    } catch (const std::exception& e) {
        Telemetry::instance().recordError();
        MLOG_E("processQueryRequest[{}]: failed: {}", clientId, e.what());
        sendResponse(clientId, msg.header.seq, false,
            {{"error", boost::json::value(e.what())}});
    }
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

void RawEndpoint::sendEventToClient(uint64_t clientId, const StoredEventData& event_data)
{
    std::lock_guard<std::mutex> lock(_clientsMutex);
    auto it = _clients.find(clientId);
    if (it == _clients.end() || !it->second) {
        MLOG_W("sendEventToClient[{}]: client not found", clientId);
        return;
    }

    // Create MessageHeader for push event to client
    lunaricorn::internal::MessageHeader hdr;
    hdr.magic = lunaricorn::internal::HeaderMagic;
    hdr.version = lunaricorn::internal::PROTOCOL_VERSION;
    hdr.type = lunaricorn::internal::MessageType::MT_PubReq;
    hdr.data_type = lunaricorn::internal::ContentType::CT_Json;
    hdr.flags = 0;
    hdr.seq = 0; // pushes have no seq
    hdr.data_len = 0;
    hdr.crc = 0;

    // Build payload
    boost::json::object payload;
    payload["type"] = boost::json::value(event_data.event_type);
    if (event_data.source.has_value()) {
        payload["source"] = boost::json::value(*event_data.source);
    } else {
        payload["source"] = boost::json::value(std::string("unknown"));
    }
    payload["payload"] = event_data.payload;
    payload["timestamp"] = boost::json::value(static_cast<int64_t>(event_data.timestamp));

    // Add tags
    boost::json::array tags_arr;
    for (const auto& tag : event_data.tags) {
        tags_arr.push_back(boost::json::value(tag));
    }
    payload["tags"] = std::move(tags_arr);

    // Add affected if present
    if (event_data.affected.is_array() && !event_data.affected.as_array().empty()) {
        payload["affected"] = event_data.affected;
    }

    // Send
    try {
        it->second->send_message(hdr, payload);
        MLOG_D("sendEventToClient[{}]: event {} sent", clientId, event_data.event_type);
    } catch (const std::exception& e) {
        MLOG_E("sendEventToClient[{}]: failed to send: {}", clientId, e.what());
        // If send failed, mark client as dead (will be cleaned up on next iter)
        // Don't call on_client_closed directly to avoid recursive locking
    }
}

void RawEndpoint::connectEngine(SignalingEnginePtr engine)
{
    if (!engine) {
        MLOG_E("connectEngine: engine pointer is null");
        return;
    }
    _engine = engine;
    _engine->setOnSubEvent([this](uint64_t clientId, const StoredEventData& event_data) {
        sendEventToClient(clientId, event_data);
    });
    MLOG_D("connectEngine: subscriber callback connected, engine set");
}

void RawEndpoint::handleEvent(const EventData& event)
{
    // Forward events to subscribed clients
    // This is a stub implementation - in a real system this would be more complex
    MLOG_D("Handling event: {}", event.event_type);
}

} // namespace lunaricorn
