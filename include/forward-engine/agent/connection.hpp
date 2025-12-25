
/**
 * @file connection.hpp
 * @brief 连接缓存实现
 * @details 用于管理 TCP 连接的缓存，支持空闲连接的自动清理。
 * @note v1 shared_ptr 会对cpu缓存不友好，因为会导致缓存的空闲连接被频繁地从内存中移除。
 * 并且连接池全局共享线程越多竞争越激烈，导致性能下降。
 */

 // v1

// #pragma once
// #include <unordered_map>
// #include <vector>
// #include <memory>
// #include <chrono>
// #include <boost/asio.hpp>

// namespace ngx::agent
// {
//     namespace net = boost::asio;

//     using tcp = boost::asio::ip::tcp;
//     using udp = boost::asio::ip::udp;

//     inline constexpr std::chrono::seconds acquire_timeout{5};

//     /**
//      * @brief 连接缓存容器
//      * @details 用于管理 TCP 和 UDP 连接的缓存，支持空闲连接的自动清理。
//      * @note 只管理 TCP 连接的空闲状态，UDP 连接无状态，不支持空闲清理。连接在销毁后会自动从缓存中移除。
//      */
//     class cache : public std::enable_shared_from_this<cache>
//     {
//         struct idle_connection
//         {
//             std::shared_ptr<tcp::socket> socket;
//             std::chrono::steady_clock::time_point last_used;
//         }; // struct idle_connection

//         net::awaitable<void> watchdog();
//         void notify_one();

//         static bool zombie_connection(const std::shared_ptr<tcp::socket>& socket);

//     public:
//         explicit cache(net::io_context &ioc, std::size_t max_connections = 100);
//         ~cache() = default;

//         void start();
//         void shutdown();

//         void idle(std::chrono::seconds timeout);
//         void cleanup(std::chrono::seconds interval);

//         [[nodiscard]] auto acquire_tcp(tcp::endpoint endpoint, std::chrono::seconds timeout = acquire_timeout)
//             ->net::awaitable<std::shared_ptr<tcp::socket>>;

//         auto release_tcp(std::shared_ptr<tcp::socket> socket, tcp::endpoint endpoint)
//             ->net::awaitable<void>;

//         [[nodiscard]] auto acquire_udp(std::chrono::seconds timeout = acquire_timeout)
//             ->net::awaitable<std::shared_ptr<udp::socket>>;

//         static auto release_udp()
//             ->net::awaitable<void>;

//     private:
//         net::io_context &ioc_;
//         net::steady_timer timer_;
//         net::strand<net::io_context::executor_type> strand_; // 串行器

//         std::size_t max_connections_;
//         std::size_t active_count_{0};
//         bool shutdown_{false}; // 停机标志

//         std::chrono::seconds idle_timeout_{60};
//         std::chrono::seconds cleanup_timeout_{10};

//         std::map<tcp::endpoint, std::list<idle_connection>> cache_;
//         std::list<std::weak_ptr<net::steady_timer>> waiters_;
//     }; // cache
// }

#pragma once
#include <vector>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <boost/asio.hpp>

namespace ngx::agent
{

    namespace net = boost::asio;
    
    using tcp = boost::asio::ip::tcp;

    class source;

    /**
     * @brief 连接缓存删除器
     */
    struct deleter
    {
        source *pool = nullptr;
        void operator()(tcp::socket *ptr) const;
    }; // class deleter


    using internal_ptr = std::unique_ptr<tcp::socket, deleter>;


    /**
     * @brief 连接缓存容器
     * @details 用于管理 TCP 连接的缓存，支持空闲连接的自动清理。
     * @note 每个线程独享一个连接缓存容器，线程之间不共享。
     */
    class source
    {
        struct idle_item
        {
            tcp::socket *socket;
            std::chrono::steady_clock::time_point last_used;
        }; // struct idle_item

    public:
        explicit source(net::io_context &ioc) : ioc_(ioc) {}

        ~source()
        {
            clear();
        }

        // 禁止拷贝（每个线程独享一个池子）
        source(const source &) = delete;
        source &operator=(const source &) = delete;

        
        [[nodiscard]] net::awaitable<internal_ptr> acquire_tcp(tcp::endpoint endpoint);

        void recycle(tcp::socket *s);

    private:
        static bool zombie_detection(tcp::socket *s);

        void clear();

    private:
        net::io_context &ioc_;

        // 数据结构：hash_map + stack(模拟)
        // key: 目标 ip:port
        // value: 空闲连接列表
        std::unordered_map<tcp::endpoint, std::vector<idle_item>> cache_;

        // 单个目标最大缓存数 (防止内存爆炸)
        const std::uint32_t max_cache_endpoint_ = 32;

        // 空闲连接最大存活时间 (超过则直接销毁，不检测)
        const std::chrono::seconds max_idle_time_{60};
    }; // class source

}