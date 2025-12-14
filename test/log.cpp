#include <monitor.hpp>
#include <iostream>
#include <thread>

boost::asio::awaitable<int> async_main()
{
    namespace log = ngx::log;
    log::asio::io_context io;
    const log::coroutine_log test(io.get_executor());
    std::size_t n = co_await test.console_write_fmt(log::level::info,"{},{} ", "hello","world");
    std::cout << "n = " << n << std::endl;
    co_return 0;
}

int main()
{
    namespace log = ngx::log;
    std::cout << "log test" << std::endl;
    log::asio::io_context io;
    boost::asio::co_spawn(io, async_main(), boost::asio::detached);
    std::jthread t ([&io](){ io.run(); });
    return 0;
}
