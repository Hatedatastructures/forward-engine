#include <agent/connection.hpp>
#include <agent/distributor.hpp>
#include <agent/session.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <array>
#include <atomic>
#include <chrono>
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
    boost::system::error_code accept_ec;
    auto accept_token = net::redirect_error(net::use_awaitable, accept_ec);
    tcp::socket socket = co_await acceptor.async_accept(accept_token);
    if (accept_ec)
    {
        co_return;
    }

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
    boost::system::error_code accept_ec;
    auto accept_token = net::redirect_error(net::use_awaitable, accept_ec);
    tcp::socket socket = co_await acceptor.async_accept(accept_token);
    if (accept_ec)
    {
        co_return;
    }
    std::make_shared<agent::session<tcp::socket>>(ioc, std::move(socket), dist, std::move(ssl_ctx))->start();
}

net::awaitable<std::string> read_proxy_connect_response(tcp::socket &socket)
{
    std::string response;
    response.reserve(256);
    std::array<char, 512> buf{};
    while (response.find("\r\n\r\n") == std::string::npos)
    {
        boost::system::error_code ec;
        auto token = net::redirect_error(net::use_awaitable, ec);
        const std::size_t n = co_await socket.async_read_some(net::buffer(buf), token);
        if (ec)
        {
            throw std::runtime_error("proxy response read failed: " + ec.message());
        }
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

    co_return response;
}

net::awaitable<void> emit_cancel_after(net::cancellation_signal &signal, const std::chrono::milliseconds timeout)
{
    net::steady_timer timer(co_await net::this_coro::executor);
    timer.expires_after(timeout);

    boost::system::error_code ec;
    auto token = net::redirect_error(net::use_awaitable, ec);
    co_await timer.async_wait(token);
    if (!ec)
    {
        signal.emit(net::cancellation_type::all);
    }
}

net::awaitable<void> wait_until_true(std::atomic_bool &flag, const std::chrono::milliseconds timeout)
{
    auto executor = co_await net::this_coro::executor;
    net::steady_timer timer(executor);

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!flag.load())
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            throw std::runtime_error("timeout waiting for expected shutdown");
        }

        timer.expires_after(std::chrono::milliseconds(10));
        boost::system::error_code ec;
        auto token = net::redirect_error(net::use_awaitable, ec);
        co_await timer.async_wait(token);
        if (ec)
        {
            co_return;
        }
    }
}

net::awaitable<void> proxy_connect_client_echo(const tcp::endpoint proxy_ep, const tcp::endpoint echo_ep)
{
    tcp::socket socket(co_await net::this_coro::executor);
    co_await socket.async_connect(proxy_ep, net::use_awaitable);

    const std::string connect_request =
        "CONNECT 127.0.0.1:" + std::to_string(echo_ep.port()) + " HTTP/1.1\r\n"
                                                                "Host: 127.0.0.1:" +
        std::to_string(echo_ep.port()) + "\r\n"
                                         "\r\n";

    co_await net::async_write(socket, net::buffer(connect_request), net::use_awaitable);

    static_cast<void>(co_await read_proxy_connect_response(socket));

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

net::awaitable<void> upstream_close_after_accept(tcp::acceptor &acceptor, const std::chrono::milliseconds delay)
{
    boost::system::error_code accept_ec;
    auto accept_token = net::redirect_error(net::use_awaitable, accept_ec);
    tcp::socket socket = co_await acceptor.async_accept(accept_token);
    if (accept_ec)
    {
        co_return;
    }

    net::steady_timer timer(co_await net::this_coro::executor);
    timer.expires_after(delay);
    boost::system::error_code wait_ec;
    co_await timer.async_wait(net::redirect_error(net::use_awaitable, wait_ec));

    boost::system::error_code close_ec;
    socket.shutdown(tcp::socket::shutdown_both, close_ec);
    socket.close(close_ec);
}

net::awaitable<void> upstream_wait_peer_close(tcp::acceptor &acceptor, std::atomic_bool &closed_flag)
{
    boost::system::error_code accept_ec;
    auto accept_token = net::redirect_error(net::use_awaitable, accept_ec);
    tcp::socket socket = co_await acceptor.async_accept(accept_token);
    if (accept_ec)
    {
        co_return;
    }

    net::cancellation_signal timeout_signal;
    net::co_spawn(co_await net::this_coro::executor,
        emit_cancel_after(timeout_signal, std::chrono::milliseconds(1500)),
        net::detached);

    std::array<char, 1> buf{};
    boost::system::error_code ec;
    auto token = net::bind_cancellation_slot(timeout_signal.slot(), net::redirect_error(net::use_awaitable, ec));
    const std::size_t n = co_await socket.async_read_some(net::buffer(buf), token);
    if (n == 0 || (ec && ec != net::error::operation_aborted))
    {
        closed_flag.store(true);
    }
}

net::awaitable<void> proxy_connect_client_expect_close(const tcp::endpoint proxy_ep, const tcp::endpoint upstream_ep)
{
    tcp::socket socket(co_await net::this_coro::executor);
    co_await socket.async_connect(proxy_ep, net::use_awaitable);

    const std::string connect_request =
        "CONNECT 127.0.0.1:" + std::to_string(upstream_ep.port()) + " HTTP/1.1\r\n"
                                                                    "Host: 127.0.0.1:" +
        std::to_string(upstream_ep.port()) + "\r\n"
                                             "\r\n";

    co_await net::async_write(socket, net::buffer(connect_request), net::use_awaitable);
    static_cast<void>(co_await read_proxy_connect_response(socket));

    net::cancellation_signal timeout_signal;
    net::co_spawn(co_await net::this_coro::executor,
        emit_cancel_after(timeout_signal, std::chrono::milliseconds(1500)),
        net::detached);

    std::array<char, 1> one{};
    boost::system::error_code ec;
    auto token = net::bind_cancellation_slot(timeout_signal.slot(), net::redirect_error(net::use_awaitable, ec));
    const std::size_t n = co_await socket.async_read_some(net::buffer(one), token);

    if (ec == net::error::operation_aborted)
    {
        throw std::runtime_error("timeout waiting for proxy to close client");
    }

    if (!ec && n != 0)
    {
        throw std::runtime_error("expected close but received data");
    }

    boost::system::error_code close_ec;
    socket.shutdown(tcp::socket::shutdown_both, close_ec);
    socket.close(close_ec);
}

net::awaitable<void> proxy_connect_client_then_close(const tcp::endpoint proxy_ep, const tcp::endpoint upstream_ep)
{
    tcp::socket socket(co_await net::this_coro::executor);
    co_await socket.async_connect(proxy_ep, net::use_awaitable);

    const std::string connect_request =
        "CONNECT 127.0.0.1:" + std::to_string(upstream_ep.port()) + " HTTP/1.1\r\n"
                                                                    "Host: 127.0.0.1:" +
        std::to_string(upstream_ep.port()) + "\r\n"
                                             "\r\n";

    co_await net::async_write(socket, net::buffer(connect_request), net::use_awaitable);
    static_cast<void>(co_await read_proxy_connect_response(socket));

    boost::system::error_code ec;
    socket.shutdown(tcp::socket::shutdown_both, ec);
    socket.close(ec);
}

net::awaitable<void> run_case_echo(agent::net::io_context &ioc, agent::distributor &dist, std::shared_ptr<ssl::context> ssl_ctx)
{
    tcp::acceptor echo_acceptor(ioc, tcp::endpoint(net::ip::make_address("127.0.0.1"), 0));
    tcp::acceptor proxy_acceptor(ioc, tcp::endpoint(net::ip::make_address("127.0.0.1"), 0));

    const auto echo_ep = echo_acceptor.local_endpoint();
    const auto proxy_ep = proxy_acceptor.local_endpoint();

    net::co_spawn(ioc, echo_server(echo_acceptor), net::detached);
    net::co_spawn(ioc, proxy_accept_one(proxy_acceptor, ioc, dist, std::move(ssl_ctx)), net::detached);
    co_await proxy_connect_client_echo(proxy_ep, echo_ep);
}

net::awaitable<void> run_case_upstream_close_should_close_client(agent::net::io_context &ioc, agent::distributor &dist,
    std::shared_ptr<ssl::context> ssl_ctx)
{
    tcp::acceptor upstream_acceptor(ioc, tcp::endpoint(net::ip::make_address("127.0.0.1"), 0));
    tcp::acceptor proxy_acceptor(ioc, tcp::endpoint(net::ip::make_address("127.0.0.1"), 0));

    const auto upstream_ep = upstream_acceptor.local_endpoint();
    const auto proxy_ep = proxy_acceptor.local_endpoint();

    net::co_spawn(ioc, upstream_close_after_accept(upstream_acceptor, std::chrono::milliseconds(50)), net::detached);
    net::co_spawn(ioc, proxy_accept_one(proxy_acceptor, ioc, dist, std::move(ssl_ctx)), net::detached);

    co_await proxy_connect_client_expect_close(proxy_ep, upstream_ep);
}

net::awaitable<void> run_case_client_close_should_close_upstream(agent::net::io_context &ioc, agent::distributor &dist,
    std::shared_ptr<ssl::context> ssl_ctx)
{
    tcp::acceptor upstream_acceptor(ioc, tcp::endpoint(net::ip::make_address("127.0.0.1"), 0));
    tcp::acceptor proxy_acceptor(ioc, tcp::endpoint(net::ip::make_address("127.0.0.1"), 0));

    const auto upstream_ep = upstream_acceptor.local_endpoint();
    const auto proxy_ep = proxy_acceptor.local_endpoint();

    std::atomic_bool upstream_closed{false};

    net::co_spawn(ioc, upstream_wait_peer_close(upstream_acceptor, upstream_closed), net::detached);
    net::co_spawn(ioc, proxy_accept_one(proxy_acceptor, ioc, dist, std::move(ssl_ctx)), net::detached);

    co_await proxy_connect_client_then_close(proxy_ep, upstream_ep);
    co_await wait_until_true(upstream_closed, std::chrono::milliseconds(1500));
}

net::awaitable<void> run_all_tests(agent::net::io_context &ioc, agent::distributor &dist, std::shared_ptr<ssl::context> ssl_ctx)
{
    co_await run_case_echo(ioc, dist, ssl_ctx);
    co_await run_case_upstream_close_should_close_client(ioc, dist, ssl_ctx);
    co_await run_case_client_close_should_close_upstream(ioc, dist, ssl_ctx);
}

int main()
{
    try
    {
        agent::net::io_context ioc;

        agent::source pool(ioc);
        agent::distributor dist(pool, ioc);

        auto ssl_ctx = std::make_shared<ssl::context>(ssl::context::tlsv12);
        ssl_ctx->set_verify_mode(ssl::verify_none);

        std::exception_ptr test_error;
        net::co_spawn(ioc,
            run_all_tests(ioc, dist, ssl_ctx),
            [&ioc, &test_error](const std::exception_ptr ep) noexcept
            {
                test_error = ep;
                ioc.stop();
            });

        ioc.run();

        if (test_error)
        {
            std::rethrow_exception(test_error);
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "session_test failed: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "session_test passed" << std::endl;
    return 0;
}
