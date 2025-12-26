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
#include <limit/blacklist.hpp>

namespace ngx::agent
{
    namespace net = boost::asio;

    using tcp_pointer = std::shared_ptr<session<net::ip::tcp::socket>>;
    using udp_pointer = std::shared_ptr<session<net::ip::udp::socket>>;
    using connection_type = std::variant<tcp_pointer, udp_pointer>;

    /**
     * @brief 分发容器
     * @note 容器负责将数据分发到对应的会话中实现代理功能
     */
    class distributor
    {
    public:
        explicit distributor(source &pool, net::io_context &ioc)
            : pool_(pool), resolver_(ioc) {}

        // 接口 1：给 HTTP 正向代理用 (需要 DNS 解析)
        net::awaitable<internal_ptr> route_forward(std::string host, std::string port)
        {
            // 1. 查 DNS
            if (blacklist_.domain(host))
            {
                throw std::runtime_error("Domain blacklisted");
            }
            auto results = co_await resolver_.async_resolve(host, port, net::use_awaitable);
            // 2. 找池子要连接
            co_return co_await pool_.acquire_tcp(*results.begin());
        }

        // 接口 2：给 HTTP 反向代理用 (查静态表)
        net::awaitable<internal_ptr> route_reverse(std::string host)
        {
            // 1. 查配置表 (比如 configuration.json 加载进来的 map)
            if (auto it = reverse_map_.find(host); it != reverse_map_.end())
            {
                co_return co_await pool_.acquire_tcp(it->second);
            }
            throw std::runtime_error("Unknown host");
        }

        // 接口 3：给 Obscura 用 (它直接给出了 IP)
        net::awaitable<internal_ptr> route_direct(tcp::endpoint ep)
        {
            co_return co_await pool_.acquire_tcp(ep);
        }

    private:
        source &pool_;
        tcp::resolver resolver_;
        limit::blacklist blacklist_;
        std::unordered_map<std::string, tcp::endpoint> reverse_map_;
    };
}
