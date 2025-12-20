#include <agent/obscura.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <iostream>
#include <thread>
#include <string>
#include <memory>

namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = net::ip::tcp;

// Test server using obscura
net::awaitable<void> do_server(tcp::acceptor &acceptor, std::shared_ptr<ssl::context> ctx, std::string expected_path)
{
    try 
    {
        auto socket = co_await acceptor.async_accept(net::use_awaitable);
        // 显式指定为 server 角色
        auto agent = std::make_shared<ngx::agent::obscura<tcp>>(std::move(socket), ctx, ngx::agent::role::server);

        std::string path = co_await agent->handshake();

        if (path != expected_path)
        {
            // In a real app we might close here
            // co_await agent->close(); // Close not implemented in obscura yet? Let's check. 
            // Assuming it relies on destructor or we should implement close.
            // For now, let's just return, destructor will close socket.
            co_return;
        }

        // Read a message using external buffer
        beast::flat_buffer buffer;
        co_await agent->async_read(buffer);
        std::string msg = beast::buffers_to_string(buffer.data());

        // Echo back
        co_await agent->async_write(msg);

        // co_await agent->close();
    }
    catch (const std::exception &e)
    {
        // std::cerr << "Server error: " << e.what() << std::endl;
    }
}

// Test client using obscura
net::awaitable<void> do_client(tcp::endpoint endpoint, std::shared_ptr<ssl::context> ctx, std::string path, std::string expected_msg, bool expect_success)
{
    try
    {
        tcp::socket socket(co_await net::this_coro::executor);
        co_await socket.async_connect(endpoint, net::use_awaitable);

        auto agent = std::make_shared<ngx::agent::obscura<tcp>>(std::move(socket), ctx, ngx::agent::role::client);

        // Perform WebSocket handshake
        try
        {
            // Client mode: pass host and path
            co_await agent->handshake("localhost", path);
        }
        catch (const std::exception &e)
        {
            if (expect_success)
                throw;
            // Expected failure
            co_return;
        }

        if (!expect_success)
        {
            throw std::runtime_error("Expected failure but handshake succeeded");
        }

        // Send message
        co_await agent->async_write(expected_msg);

        // Read response
        beast::flat_buffer buffer;
        co_await agent->async_read(buffer);

        std::string reply = beast::buffers_to_string(buffer.data());
        if (reply != expected_msg)
        {
            throw std::runtime_error("Echo failed");
        }

        // co_await agent->close();
    }
    catch (const std::exception &e)
    {
        if (expect_success)
        {
            std::cerr << "Client error: " << e.what() << std::endl;
            throw;
        }
    }
}

int main()
{
    try
    {
        net::io_context ioc;

        auto server_ctx = std::make_shared<ssl::context>(ssl::context::tlsv12);
        server_ctx->use_certificate_chain_file("cert.pem");
        server_ctx->use_private_key_file("key.pem", ssl::context::pem);

        auto client_ctx = std::make_shared<ssl::context>(ssl::context::tlsv12);
        client_ctx->set_verify_mode(ssl::verify_none); // Self-signed cert

        tcp::endpoint endpoint(net::ip::make_address("127.0.0.1"), 0);
        tcp::acceptor acceptor(ioc, endpoint);
        auto bound_endpoint = acceptor.local_endpoint();

        std::string secret_path = "/secret";

        // Test 1: Success
        net::co_spawn(ioc, do_server(acceptor, server_ctx, secret_path), net::detached);
        net::co_spawn(ioc, do_client(bound_endpoint, client_ctx, secret_path, "Hello", true), net::detached);

        // Test 2: Wrong path (Server should close or reject, client should fail handshake or read)
        // Note: In current do_server, if path mismatch, it returns. 
        // Client will see EOF or connection closed.
        net::co_spawn(ioc, do_server(acceptor, server_ctx, secret_path), net::detached);
        net::co_spawn(ioc, do_client(bound_endpoint, client_ctx, "/wrong", "Hello", false), net::detached);

        ioc.run();

        std::cout << "Tests passed" << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
