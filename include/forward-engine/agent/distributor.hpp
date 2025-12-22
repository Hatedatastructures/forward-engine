#pragma once

#include <atomic>
#include <unordered_map>
#include <memory>
#include <string>
#include <boost/asio.hpp>
#include <agent/frame.hpp>
#include <agent/obscura.hpp>
#include <agent/session.hpp>

namespace ngx::agent
{
    namespace net = boost::asio;


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
    private:
        std::unordered_map<std::string,std::shared_ptr<session<net::ip::tcp>>> maps;
    };
}
