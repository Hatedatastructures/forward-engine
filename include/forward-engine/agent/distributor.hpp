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

    class distributor
    {
    public:
        distributor() = default;
        ~distributor() = default;
    private:
        std::unordered_map<std::string,std::shared_ptr<session<net::ip::tcp>>> maps;
    };
}
