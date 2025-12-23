#pragma once

#include <atomic>
#include <unordered_map>
#include <memory>
#include <string>
#include <variant>
#include <boost/asio.hpp>
#include "frame.hpp"
#include "obscura.hpp"
#include "session.hpp"
#include "connection.hpp"

namespace ngx::agent
{
    namespace net = boost::asio;

    using tcp_pointer = std::shared_ptr<session<net::ip::tcp::socket>>;
    using udp_pointer = std::shared_ptr<session<net::ip::udp::socket>>;
    using connection_type = std::variant<tcp_pointer, udp_pointer>;

    /**
     * @brief 数据分发容器
     * @note 容器负责将数据分发到对应的会话中实现代理功能
     */
    class distributor 
    {
        using tcp = boost::asio::ip::tcp;
        using cache_ptr = std::shared_ptr<cache>;
        static constexpr size_t shard_count = 4; // 分片数量

    public:
        explicit distributor(net::io_context& ioc);
        ~distributor() = default;
        
        void shutdown();
        
        /**
         * @brief 获取指定端点的连接缓存分片
         * @param endpoint 目标端点
         * @return std::shared_ptr<cache> 对应的缓存实例
         */
        cache_ptr get_cache(const tcp::endpoint& endpoint);

        /**
         * @brief 获取 UDP 连接缓存分片 (轮询)
         * @return std::shared_ptr<cache> 对应的缓存实例
         */
        cache_ptr get_udp_cache();

    private:
        net::io_context& ioc_;
        std::vector<cache_ptr> caches_; // 缓存分片池
        std::atomic<size_t> udp_counter_{0};
        
        std::unordered_map<std::string,connection_type> maps; // variant 实现同时存储 tcp 和 udp 会话
    }; // class distributor
}
