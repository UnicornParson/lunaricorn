#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <memory>

namespace lunaricorn {

class Leader;

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;
using tcp = net::ip::tcp;

class Endpoint : public std::enable_shared_from_this<Endpoint> {
public:
    Endpoint(net::io_context& ioc, std::shared_ptr<Leader> leader);
    void run(tcp::endpoint endpoint);

private:
    class Session;

    net::io_context& ioc_;
    std::unique_ptr<tcp::acceptor> acceptor_;
    std::shared_ptr<Leader> leader_;

    void do_accept();
};

} // namespace lunaricorn