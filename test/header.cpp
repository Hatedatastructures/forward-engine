#include <http.hpp>
#include <iostream>
#include <string>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>


namespace net = boost::asio;
namespace websocket = boost::beast::websocket;
using tcp = boost::asio::ip::tcp;

net::awaitable<void> test()
{
    // net::io_context ioc;
    // tcp::resolver resolver(ioc);
    // auto const results = co_await resolver.async_resolve("127.0.0.1", "443");
    // tcp::socket socket(ioc);
    // net::ssl::stream<tcp::socket> tcp_stream(std::move(socket), ioc.get_executor());
    // co_await 
    // co_return;
    // net::ip::udp::resolver;
    // net::ip::udp::socket;
    // tcp::endpoint;
    // tcp::resolver;
    // tcp::socket;
}

int main()
{
    // test();
    // namespace http = ngx::http;
    // http::headers headers;
    // headers.construct("Content-Type", "text/html");
    // headers.construct("Content-Length", "123");
    // headers.set("Content-Type", "text/plain");
    // std::cout << headers.size() << std::endl;

    // for (const auto &header : headers)
    // {
    //     std::cout << header.key.value() << " " << header.value << std::endl;
    // }
    // return 0;
}
