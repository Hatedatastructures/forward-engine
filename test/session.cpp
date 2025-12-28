#include <agent/connection.hpp>
#include <agent/distributor.hpp>
#include <agent/session.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <array>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;

namespace agent = ngx::agent;

/**
 * @brief 回显服务器
 * @param acceptor 监听 acceptor
 */
net::awaitable<void> echo_server(tcp::acceptor &acceptor)
{
    tcp::socket socket = co_await acceptor.async_accept(net::use_awaitable);
    std::array<char, 8192> buf{};
    while (true)
    {
        boost::system::error_code ec;
        auto token = net::redirect_error(net::use_awaitable, ec);
        const std::size_t n = co_await socket.async_read_some(net::buffer(buf), token);
        if (ec || n == 0)
        {
            break;
        }
        co_await net::async_write(socket, net::buffer(buf.data(), n), token);
        if (ec)
        {
            break;
        }
    }
}

net::awaitable<void> proxy_accept_one(tcp::acceptor &acceptor, agent::net::io_context &ioc,
    agent::distributor &dist, std::shared_ptr<ssl::context> ssl_ctx)
{
    tcp::socket socket = co_await acceptor.async_accept(net::use_awaitable);
    std::make_shared<agent::session<tcp::socket>>(ioc, std::move(socket), dist, std::move(ssl_ctx))->start();
}

net::awaitable<void> proxy_connect_client(const tcp::endpoint proxy_ep, const tcp::endpoint echo_ep)
{
    tcp::socket socket(co_await net::this_coro::executor);
    co_await socket.async_connect(proxy_ep, net::use_awaitable);

    const std::string connect_request =
        "CONNECT 127.0.0.1:" + std::to_string(echo_ep.port()) + " HTTP/1.1\r\n"
                                                                "Host: 127.0.0.1:" +
        std::to_string(echo_ep.port()) + "\r\n"
                                         "\r\n";

    co_await net::async_write(socket, net::buffer(connect_request), net::use_awaitable);

    std::string response;
    response.reserve(256);
    std::array<char, 512> buf{};
    while (response.find("\r\n\r\n") == std::string::npos)
    {
        const std::size_t n = co_await socket.async_read_some(net::buffer(buf), net::use_awaitable);
        if (n == 0)
        {
            throw std::runtime_error("proxy response eof");
        }
        response.append(buf.data(), n);
        if (response.size() > 8192)
        {
            throw std::runtime_error("proxy response too large");
        }
    }

    if (!response.starts_with("HTTP/1.1 200"))
    {
        throw std::runtime_error("proxy connect failed: " + response);
    }

    const std::string payload = "hello_forward_engine";
    co_await net::async_write(socket, net::buffer(payload), net::use_awaitable);

    std::string echo;
    echo.resize(payload.size());
    std::size_t got = 0;
    while (got < payload.size())
    {
        got += co_await socket.async_read_some(net::buffer(echo.data() + got, payload.size() - got), net::use_awaitable);
    }

    if (echo != payload)
    {
        throw std::runtime_error("echo mismatch");
    }

    boost::system::error_code ec;
    socket.shutdown(tcp::socket::shutdown_both, ec);
    socket.close(ec);
}

int main()
{
    try
    {
        agent::net::io_context ioc;

        agent::source pool(ioc);
        agent::distributor dist(pool, ioc);

        tcp::acceptor echo_acceptor(ioc, tcp::endpoint(net::ip::make_address("127.0.0.1"), 0));
        tcp::acceptor proxy_acceptor(ioc, tcp::endpoint(net::ip::make_address("127.0.0.1"), 0));

        const auto echo_ep = echo_acceptor.local_endpoint();
        const auto proxy_ep = proxy_acceptor.local_endpoint();

        auto ssl_ctx = std::make_shared<ssl::context>(ssl::context::tlsv12);
        ssl_ctx->set_verify_mode(ssl::verify_none);

        net::co_spawn(ioc, echo_server(echo_acceptor), net::detached);
        net::co_spawn(ioc, proxy_accept_one(proxy_acceptor, ioc, dist, ssl_ctx), net::detached);
        net::co_spawn(ioc, [proxy_ep, echo_ep, &ioc]() -> net::awaitable<void>
                      {
                          co_await proxy_connect_client(proxy_ep, echo_ep);
                          ioc.stop(); }, net::detached);

        ioc.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "session_test failed: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "session_test passed" << std::endl;
    return 0;
}
