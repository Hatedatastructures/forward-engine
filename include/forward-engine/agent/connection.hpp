#pragma once
#include <map>
#include <list>
#include <memory>
#include <chrono>
#include <boost/asio.hpp>

namespace ngx::agent
{
    namespace net = boost::asio;

    using tcp = boost::asio::ip::tcp;
    using udp = boost::asio::ip::udp;

    inline constexpr std::chrono::seconds acquire_timeout{5};

    /**
     * @brief 连接缓存容器
     * @details 用于管理 TCP 和 UDP 连接的缓存，支持空闲连接的自动清理。
     * @note 只管理 TCP 连接的空闲状态，UDP 连接无状态，不支持空闲清理。连接在销毁后会自动从缓存中移除。
     */
    class cache : public std::enable_shared_from_this<cache>
    {
        struct idle_connection
        {
            std::shared_ptr<tcp::socket> socket_;
            std::chrono::steady_clock::time_point last_used;
        }; // struct idle_connection

        net::awaitable<void> watchdog();
        void notify_one();

    public:
        explicit cache(net::io_context &ioc, std::size_t max_connections = 100);
        ~cache() = default;

        void start();
        void shutdown();

        void idle(std::chrono::seconds timeout);
        void cleanup(std::chrono::seconds interval);

        [[nodiscard]] auto acquire_tcp(tcp::endpoint endpoint, std::chrono::seconds timeout = acquire_timeout)
            ->net::awaitable<std::shared_ptr<tcp::socket>>;

        auto release_tcp(std::shared_ptr<tcp::socket> socket, tcp::endpoint endpoint)
            ->net::awaitable<void>;

        [[nodiscard]] auto acquire_udp(std::chrono::seconds timeout = acquire_timeout)
            ->net::awaitable<std::shared_ptr<udp::socket>>;

        static auto release_udp()
            ->net::awaitable<void>;

    private:
        net::io_context &ioc_;
        net::steady_timer timer_;
        net::strand<net::io_context::executor_type> strand_; // 串行器

        std::size_t max_connections_;
        std::size_t active_count_{0};
        bool shutdown_{false}; // 停机标志

        std::chrono::seconds idle_timeout_{60};
        std::chrono::seconds cleanup_timeout_{10};

        std::map<tcp::endpoint, std::list<idle_connection>> cache_;
        std::list<std::weak_ptr<net::steady_timer>> waiters_;
    }; // cache
}