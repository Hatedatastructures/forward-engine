#pragma once

#include <unordered_map>
#include <memory>
#include <string>
#include <variant>
#include <boost/asio.hpp>
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
        explicit distributor(source &pool, net::io_context &ioc);
        [[nodiscard]] net::awaitable<internal_ptr> route_reverse(std::string host);
        [[nodiscard]] net::awaitable<internal_ptr> route_direct(tcp::endpoint ep) const;
        [[nodiscard]] net::awaitable<internal_ptr> route_forward(std::string host, std::string port);
    private:
        source &pool_;
        tcp::resolver resolver_;
        limit::blacklist blacklist_;
        std::unordered_map<std::string, tcp::endpoint> reverse_map_;
    }; // class distributor
}
