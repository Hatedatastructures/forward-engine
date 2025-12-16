#include <log/monitor.hpp>
#include <iostream>
#include <thread>

boost::asio::awaitable<int> async_main(const ngx::log::coroutine_log &log_instance)
{
    namespace log = ngx::log;
    std::size_t write_size = co_await log_instance.console_write_fmt(log::level::info, "{},{} ", "hello", "world");
    std::cout << "n = " << write_size << std::endl;
    co_return 0;
}

int main()
{
    namespace log = ngx::log;
    std::cout << "log test" << std::endl;

    log::asio::io_context io_context;
    log::coroutine_log log_instance(io_context.get_executor());

    boost::asio::co_spawn(io_context, async_main(log_instance), boost::asio::detached);

    std::jthread io_thread([&io_context]()
    {
        io_context.run();
    });

    return 0;
}
