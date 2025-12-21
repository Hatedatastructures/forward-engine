#include <http.hpp>
#include <memory>
#include <agent/session.hpp>
#include <agent/distributor.hpp>
#include <thread>
#include <log/monitor.hpp>
#include <iostream>

namespace agent = ngx::agent;
namespace http = ngx::http;

/**
 * @brief 连接会话测试函数
 * @param io_context `io_context` 上下文
 * @param log_console 日志对象
 * @param server_address 服务器地址
 * @param server_port 服务器端口
 * @return `net::awaitable<void>`
 */
auto connect_session(agent::net::io_context &io_context, std::shared_ptr<ngx::log::coroutine_log> log_console,
                     const std::string &server_address, std::uint16_t server_port)
    -> agent::net::awaitable<void>
{
    std::string error_message;
    try
    {
        // 创建 `session` 实例
        auto session = std::make_shared<agent::session<agent::tcp>>(io_context);

        // 异步连接到目标服务器
        co_await session->async_connect(agent::tcp::endpoint(agent::net::ip::make_address(server_address), server_port));

        // 构造 HTTP 请求
        http::request request;
        request.method(http::verb::get);
        request.target("/");
        request.version(11);
        request.set(http::field::host, server_address + ":" + std::to_string(server_port));
        request.set(http::field::user_agent, "ForwardEngine/1.0");
        request.set(http::field::accept, "*/*");

        // 发送请求
        co_await session->async_write(http::serialize(request));

        // 异步读取响应数据
        std::string buffer;
        co_await session->async_read(buffer);

        http::response response;
        if (http::deserialize(buffer, response))
        {
            co_await log_console->console_write_fmt(ngx::log::level::debug, "收到响应: {}", buffer);
        }
        else
        {
            co_await log_console->console_write_fmt(ngx::log::level::error, "解析响应失败: {}", buffer);
        }

        session->close();
    }
    catch (const std::exception &e)
    {
        error_message = e.what();
    }

    // 在 catch 块外部处理异常日志，因为 C++20 不允许在 catch 块内使用 co_await
    if (!error_message.empty())
    {
        if (log_console)
        {
            co_await log_console->console_write_fmt(ngx::log::level::error, "连接会话发生异常: {}", error_message);
        }
        else
        {
            std::cerr << "连接会话发生异常: " << error_message << std::endl;
        }
    }

    co_return;
}

// TODO: 添加更多测试用例
int main()
{
    std::cout << "测试程序启动..." << std::endl;

    const std::string address = "124.71.136.228";

    try
    {
        constexpr std::uint16_t port = 6779;
        agent::net::io_context io_context;
        auto log_console = std::make_shared<ngx::log::coroutine_log>(io_context.get_executor());

        // 启动协程任务
        agent::net::co_spawn(io_context, connect_session(io_context, log_console, address, port),
                             agent::net::detached);

        std::cout << "IO 上下文开始运行..." << std::endl;

        auto funcion = [&io_context]()
        {
            try
            {
                io_context.run();
            }
            catch (const std::exception &e)
            {
                std::cerr << "IO 线程异常: " << e.what() << std::endl;
            }
        };

        std::jthread io_thread(funcion);
    }
    catch (const std::exception &e)
    {
        std::cerr << "主程序发生异常: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "测试程序正常退出" << std::endl;
    return 0;
}
