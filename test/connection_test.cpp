#include <agent/connection.hpp>
#include <agent/distributor.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <cassert>
#include <thread>

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// 简单的 Echo 服务器用于接受连接
net::awaitable<void> echo_server(tcp::acceptor &acceptor)
{
    while (true)
    {
        try
        {
            auto socket = co_await acceptor.async_accept(net::use_awaitable);
            // 保持连接打开，不需要读写
            // 使用分离的协程保持 socket 存活一段时间或直到对端关闭
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
            break;
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

    // 创建限制为 2 的连接池
    auto pool = std::make_shared<ngx::agent::cache>(ioc, 2);
    pool->start();

    try
    {
        std::cout << "[Test] Step 1: Acquire 2 connections" << std::endl;
        auto c1 = co_await pool->acquire_tcp(endpoint);
        std::cout << "  Got c1" << std::endl;
        auto c2 = co_await pool->acquire_tcp(endpoint);
        std::cout << "  Got c2" << std::endl;

        // 验证它们是不同的
        assert(c1 != c2);

        std::cout << "[Test] Step 2: Try acquire 3rd (should timeout)" << std::endl;
        bool timed_out = false;
        try
        {
            // 为测试设置短超时
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
        // 保存原始指针以检查复用
        auto c1_ptr = c1.get();
        // 释放 c1 到连接池
        co_await pool->release_tcp(c1, endpoint);
        // c1 现在由连接池管理，但调用者的 shared_ptr 仍然有效？
        // 不，release_tcp 按值传递，但我们传递了我们的副本。
        // 为了安全起见，我们应该重置我们的副本，或者只需知道连接池有一个副本。
        c1.reset();

        // 再次尝试获取
        auto c3 = co_await pool->acquire_tcp(endpoint, std::chrono::seconds(2));
        std::cout << "  Got c3" << std::endl;
        assert(c3.get() == c1_ptr); // 应该复用 c1
        std::cout << "  Reuse confirmed." << std::endl;

        std::cout << "[Test] Step 4: LRU Eviction Test" << std::endl;
        // 当前活动：c2, c3。限制 2。
        // 将两者都释放到连接池。
        co_await pool->release_tcp(c2, endpoint);
        c2.reset();

        co_await pool->release_tcp(c3, endpoint);
        c3.reset();

        // 现在连接池有 2 个空闲连接（c1_ptr 和 c2_ptr）。
        // 尝试获取不同的端点。
        // 使用一个肯定已关闭的端口（希望如此）。
        tcp::endpoint bad_endpoint(net::ip::make_address("127.0.0.1"), port + 100);

        bool connection_refused = false;
        try
        {
            // 这应该触发一个空闲连接的驱逐。
            auto c_new = co_await pool->acquire_tcp(bad_endpoint);
            std::cout << "  Connected to bad endpoint? (Unexpected but allowed)" << std::endl;
            // 如果连接成功，意味着驱逐起作用了（插槽已释放）并且连接成功。
            connection_refused = true; // 视为测试成功
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

        // 测试 UDP 分片（轮询）
        auto u_shard1 = dist.get_udp_cache();
        auto u_shard2 = dist.get_udp_cache();
        assert(u_shard1 != nullptr);
        assert(u_shard2 != nullptr);
        // 4 个分片的轮询意味着下一个应该是不同的
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

        // 设置接收器
        tcp::acceptor acceptor(ioc, tcp::endpoint(tcp::v4(), 0));
        const unsigned short port = acceptor.local_endpoint().port();
        std::cout << "Server listening on port " << port << std::endl;

        net::co_spawn(ioc, echo_server(acceptor), net::detached);
        net::co_spawn(ioc, run_test(ioc, port), net::detached);

        auto io_func = [&ioc]()
        {
            ioc.run();
        }; // 单独开线程调度任务

        std::jthread io_thread(io_func);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Main Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
