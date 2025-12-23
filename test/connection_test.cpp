#include <agent/connection.hpp>
#include <agent/distributor.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <cassert>

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// Simple Echo Server to accept connections
net::awaitable<void> echo_server(tcp::acceptor &acceptor)
{
    while (true)
    {
        try
        {
            auto socket = co_await acceptor.async_accept(net::use_awaitable);
            // Just keep it open, don't need to read/write
            // Use a detached coroutine to keep socket alive for a bit or until closed by peer
            auto task = [s = std::move(socket)]() mutable -> net::awaitable<void>
            {
                try
                {
                    char data[1024];
                    while (true)
                    {
                        auto n = co_await s.async_read_some(net::buffer(data), net::use_awaitable);
                        if (n == 0)
                            break;
                    }
                }
                catch (...)
                {
                }
                co_return;
            };

            net::co_spawn(socket.get_executor(), std::move(task), net::detached);
        }
        catch (...)
        {
            break;
        }
    }
}

net::awaitable<void> run_test(net::io_context &ioc, unsigned short port)
{
    std::cout << "[Test] Starting..." << std::endl;
    tcp::endpoint endpoint(net::ip::make_address("127.0.0.1"), port);

    // Create cache with limit 2
    auto pool = std::make_shared<ngx::agent::cache>(ioc, 2);
    pool->start();

    try
    {
        std::cout << "[Test] Step 1: Acquire 2 connections" << std::endl;
        auto c1 = co_await pool->acquire_tcp(endpoint);
        std::cout << "  Got c1" << std::endl;
        auto c2 = co_await pool->acquire_tcp(endpoint);
        std::cout << "  Got c2" << std::endl;

        // Verify they are different
        assert(c1 != c2);

        std::cout << "[Test] Step 2: Try acquire 3rd (should timeout)" << std::endl;
        bool timed_out = false;
        try
        {
            // Set short timeout for test
            auto c3 = co_await pool->acquire_tcp(endpoint, std::chrono::seconds(1));
        }
        catch (const boost::system::system_error &e)
        {
            if (e.code() == net::error::timed_out)
            {
                timed_out = true;
                std::cout << "  Timed out as expected." << std::endl;
            }
            else
            {
                std::cout << "  Unexpected error: " << e.what() << std::endl;
            }
        }
        assert(timed_out);

        std::cout << "[Test] Step 3: Release c1 and Acquire c3 (Reuse Test)" << std::endl;
        // Save raw pointer to check reuse
        auto c1_ptr = c1.get();
        // Release c1 to pool
        co_await pool->release_tcp(c1, endpoint);
        // c1 is now managed by pool, but shared_ptr in caller is still valid?
        // No, release_tcp takes by value, but we passed our copy.
        // We should reset our copy to be safe, or just know that pool has a copy.
        c1.reset();

        // Try acquire again
        auto c3 = co_await pool->acquire_tcp(endpoint, std::chrono::seconds(2));
        std::cout << "  Got c3" << std::endl;
        assert(c3.get() == c1_ptr); // Should reuse c1
        std::cout << "  Reuse confirmed." << std::endl;

        std::cout << "[Test] Step 4: LRU Eviction Test" << std::endl;
        // Current active: c2, c3. Limit 2.
        // Release both to pool.
        co_await pool->release_tcp(c2, endpoint);
        c2.reset();

        co_await pool->release_tcp(c3, endpoint);
        c3.reset();

        // Now pool has 2 idle connections (c1_ptr and c2_ptr).
        // Try acquire for DIFFERENT endpoint.
        // Use a port that is definitely closed (hopefully).
        tcp::endpoint bad_endpoint(net::ip::make_address("127.0.0.1"), port + 100);

        bool connection_refused = false;
        try
        {
            // This should trigger eviction of one idle connection.
            auto c_new = co_await pool->acquire_tcp(bad_endpoint);
            std::cout << "  Connected to bad endpoint? (Unexpected but allowed)" << std::endl;
            // If it connected, it means eviction worked (slot freed) AND connection succeeded.
            connection_refused = true; // Treat as success for test purpose
        }
        catch (const boost::system::system_error &e)
        {
            if (e.code() == net::error::connection_refused)
            {
                connection_refused = true;
                std::cout << "  Got connection refused as expected (means slot was granted)." << std::endl;
            }
            else
            {
                std::cout << "  Unexpected error during eviction test: " << e.what() << std::endl;
            }
        }
        assert(connection_refused);

        std::cout << "[Test] Step 5: Shutdown" << std::endl;
        pool->shutdown();

        std::cout << "[Test] Step 6: Distributor Sharding" << std::endl;
        ngx::agent::distributor dist(ioc);

        // Test UDP sharding (Round Robin)
        auto u_shard1 = dist.get_udp_cache();
        auto u_shard2 = dist.get_udp_cache();
        assert(u_shard1 != nullptr);
        assert(u_shard2 != nullptr);
        // Round robin with 4 shards implies next should be different
        assert(u_shard1 != u_shard2);
        std::cout << "  UDP Round Robin confirmed." << std::endl;

        dist.shutdown();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Test Failed: " << e.what() << std::endl;
        exit(1);
    }

    std::cout << "[Test] Passed." << std::endl;
    co_return;
}

int main()
{
    try
    {
        net::io_context ioc;

        // Setup acceptor
        tcp::acceptor acceptor(ioc, tcp::endpoint(tcp::v4(), 0));
        unsigned short port = acceptor.local_endpoint().port();
        std::cout << "Server listening on port " << port << std::endl;

        net::co_spawn(ioc, echo_server(acceptor), net::detached);
        net::co_spawn(ioc, run_test(ioc, port), net::detached);

        ioc.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Main Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
