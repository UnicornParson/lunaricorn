#include "raw_endpoint.h"
#include <iostream>
#include <Poco/Net/NetException.h>
#include <Poco/Timespan.h>

#include "stdafx.h"

namespace lunaricorn
{

static constexpr auto SERVER_HB_PERIOD = std::chrono::seconds(10);
static constexpr auto CLINENT_HB_PERIOD = std::chrono::seconds(10);

RawEndpoint::RawEndpoint(const std::string& ip, Poco::UInt16 port)
    : _serverSocket(Poco::Net::SocketAddress(Poco::Net::IPAddress(ip), port))
{
    _serverSocket.setReuseAddress(true);
    _serverSocket.setReusePort(true);
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
            if (!client){MLOG_E(MBUG "[stop] no client for {}", id); continue;}
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
            PClient client = std::make_shared<Client_>();
            client->sock = std::move(clientSocket);
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
        if (!client){MLOG_E(MBUG "no client for {}", id); continue;}
        const auto hb_duration = client->server_hb_delay();
        if (hb_duration >= SERVER_HB_PERIOD)
        {
            // TODO: send hb id needed
            MLOG_D("send hb to {}", id);
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
            PClient client;
            {
                std::lock_guard<std::mutex> lock(_clientsMutex);
                auto it = _clients.find(id);
                if (it == _clients.end())
                    continue;
                client = it->second;
            }
            if (!client)
            {
                MLOG_E(MBUG "id {} not found", id);
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
        if(!client){MLOG_E(MBUG "null client data for {}", clientId); return;}
        try { client->sock.close();} catch (...) {}
        const auto s =  std::chrono::duration_cast<std::chrono::seconds>(client->connect_time_delay()).count();
        MLOG_D("client {} closed. active {} seconds", clientId, s);
    } else {
        MLOG_E(MBUG "unknown client id: {}", clientId);
    }
}

// Заглушка для обработки данных
void RawEndpoint::processData(uint64_t clientId, const std::vector<char>& data)
{
    // Пример: вывод идентификатора клиента и размера данных
    std::cout << "[Client " << clientId << "] received " << data.size() << " bytes\n";
    // Здесь должна быть реальная логика обработки
}

void RawEndpoint::handleEvent(const EventData& event)
{
    // send to subscribed endpoints

}

} // namespace lunaricorn