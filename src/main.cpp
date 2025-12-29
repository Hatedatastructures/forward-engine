#include <agent.hpp>
#include <memory>
#include <thread>
#include <log/monitor.hpp>
#include <iostream>

namespace agent = ngx::agent;
namespace http = ngx::http;
namespace net = agent::net;

constexpr  std::string_view cert_path = "C:\\Users\\C1373\\Desktop\\ForwardEngine\\cert.pem";
constexpr  std::string_view key_path = "C:\\Users\\C1373\\Desktop\\ForwardEngine\\key.pem";


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
            agent::worker w(port,{cert_path.data()},{key_path.data()});
            w.run();
        });
    }

    // 3. 等待线程结束
    for (auto &t : threads)
    {
        if (t.joinable())
            t.join();
    }
}
