#include "raw_endpoint_client.h"

namespace lunaricorn
{
RE_Client::RE_Client(Poco::Net::StreamSocket&& socket): sock(std::move(socket))
{
    _count++;
}
RE_Client::~RE_Client()
{
    _count--;
}

void RE_Client::processData(uint64_t clientId, const std::vector<char>& data)
{

}

} // namespace lunaricorn