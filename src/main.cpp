#include <http.hpp>
#include <agent.hpp>
#include <memory>
#include <agent/session.hpp>
#include <agent/distributor.hpp>
#include <thread>
#include <log/monitor.hpp>
#include <iostream>

namespace agent = ngx::agent;
namespace http = ngx::http;
namespace net = ngx::agent::net;

// TODO: add more tests
int main()
{
    unsigned short port = 8080;
    // 1. 获取 CPU 核心数 (比如 16 核)
    auto const threads_count = std::thread::hardware_concurrency();
    std::vector<std::thread> threads;

    std::cout << "Starting ForwardEngine on port " << port
              << " with " << threads_count << " threads..." << std::endl;

    // 2. 启动 16 个线程，每个线程跑一个 Worker
    for (unsigned int i = 0; i < threads_count; ++i)
    {
        threads.emplace_back([port]
                             {
            ngx::agent::worker w(port);
            w.run(); });
    }

    // 3. 等待线程结束
    for (auto &t : threads)
    {
        if (t.joinable())
            t.join();
    }
}
