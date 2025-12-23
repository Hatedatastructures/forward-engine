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
    public:
        distributor() = default;
        ~distributor() = default;
        void shutdown();
    private:
        std::unordered_map<std::string,connection_type> maps; // variant 实现同时存储 tcp 和 udp 会话
    }; // class distributor
}
