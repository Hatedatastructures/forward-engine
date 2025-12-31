#pragma once

#include <memory>
#include <utility>
#include <string>
#include <string_view>
#include <array>
#include <cstddef>
#include <cctype>
#include <atomic>
#include <memory_resource>
#include <boost/asio.hpp>
#include "analysis.hpp"
#include "obscura.hpp"
#include "connection.hpp"
#include "adaptation.hpp"
#include <http/deserialization.hpp>
#include <http/serialization.hpp>

namespace ngx::agent
{
    using tcp = boost::asio::ip::tcp;

    /**
     * @brief 会话管理类
     * @tparam Transport socket 类型
     * @note 作为会话管理类，负责管理与目标服务器（或客户端）的连接，自动处理代理转发和http请求
     */
    template <socket_concept Transport>
    class session : public std::enable_shared_from_this<session<Transport>>
    {
    public:
        using socket_type = Transport;

        explicit session(net::io_context &io_context, socket_type socket, distributor &dist,
        std::shared_ptr<ssl::context> ssl_ctx);
        virtual ~session();

        void start();
        void close();

    private:
        net::awaitable<void> diversion();
        net::awaitable<void> tunnel_tcp();
        net::awaitable<void> handle_http();
        net::awaitable<void> handle_obscura();
        net::awaitable<void> tunnel_obscura(std::shared_ptr<obscura<tcp>> proto);
        net::awaitable<void> transfer_obscura_to_proto(obscura<tcp> &proto) const;
        net::awaitable<void> transfer_obscura_from_proto(obscura<tcp> &proto) const;
        /**
         * @brief 从源读取数据并写入目标
         * @param from 源
         * @param to 目标
         * @details 该函数会从源读取数据，并将数据写入目标。
         */
        template <typename Source, typename Dest>
        net::awaitable<void> transfer_tcp(Source &from, Dest &to)
        {
            std::array<char, 8192> buf{};
            try
            {
                while (true)
                {
                    const std::size_t n = co_await adaptation::async_read(from, net::buffer(buf), net::use_awaitable);
                    if (n == 0)
                    {
                        break;
                    }
                    co_await net::async_write(to, net::buffer(buf.data(), n), net::use_awaitable);
                }
            }
            catch (...)
            {
            }
        }

        net::io_context &io_context_;
        std::shared_ptr<ssl::context> ssl_ctx_;
        distributor &distributor_;
        socket_type client_socket_; // 客户端连接   
        internal_ptr upstream_;

        std::array<std::byte, 16384> buffer_{};
        std::pmr::monotonic_buffer_resource pool_;
    }; // class session
}

namespace ngx::agent
{
    template <socket_concept Transport>
    session<Transport>::session(net::io_context &io_context, socket_type socket, distributor &dist,
        std::shared_ptr<ssl::context> ssl_ctx)
    : io_context_(io_context), ssl_ctx_(std::move(ssl_ctx)), distributor_(dist),
    client_socket_(std::move(socket)), pool_(buffer_.data(), buffer_.size()) {}

    template <socket_concept Transport>
    session<Transport>::~session()
    {
        close();
    }

    template <socket_concept Transport>
    void session<Transport>::start()
    {
        auto process = [self = this->shared_from_this()]() -> net::awaitable<void>
        {
            co_await self->diversion();
        };
        net::co_spawn(io_context_, process, net::detached);
    }

    /**
     * @brief 关闭会话
     * @details 该函数会关闭与目标服务器（或客户端）的连接，释放相关资源。
     */
    template <socket_concept Transport>
    void session<Transport>::close()
    {
        if constexpr (requires { client_socket_.is_open(); })
        {
            if (client_socket_.is_open())
            {
                boost::system::error_code ec;
                client_socket_.shutdown(tcp::socket::shutdown_both, ec);
                client_socket_.close(ec);
            }
        }
        upstream_.reset();
    }

    /**
     * @brief 会话分发器
     * @details 该函数会根据请求协议类型，选择相应的处理函数。
     */
    template <socket_concept Transport>
    net::awaitable<void> session<Transport>::diversion()
    {
        try
        {
            // 1. 偷看数据 (Peek)
            std::array<char, 24> peek_buf;
            auto buf = net::buffer(peek_buf);
            // 循环 peek 确保至少读到一点数据，避免误判
            size_t n = co_await client_socket_.async_receive(buf, tcp::socket::message_peek, net::use_awaitable);

            // 2. 识别协议 (调用 analysis)
            const auto type = analysis::detect(std::string_view(peek_buf.data(), n));

            // 3. 分流
            if (type == protocol_type::http)
            {
                co_await handle_http();
            }
            else
            {
                co_await handle_obscura();
            }
        }
        catch (...)
        {
            close();
        }
    }

    /**
     * @brief 处理HTTP请求
     * @details 该函数会从客户端读取HTTP请求，并根据请求类型进行相应的处理。
     */
    template <socket_concept Transport>
    net::awaitable<void> session<Transport>::handle_http()
    {
        pool_.release();
        http::request req(&pool_);
        const bool success = co_await http::async_read(client_socket_, req, &pool_);

        if (!success)
        {
            co_return;
        }


        //  连接上游
        const auto target = analysis::resolve(req);
        if (target.forward_proxy)
        {
            upstream_ = co_await distributor_.route_forward(target.host, target.port);
        } 
        else 
        {
            upstream_ = co_await distributor_.route_reverse(target.host);
        }
        
        if (!upstream_) co_return;

        // 转发
        if (req.method() == http::verb::connect)
        {
            const std::string resp = "HTTP/1.1 200 Connection Established\r\n\r\n";
            co_await net::async_write(client_socket_, net::buffer(resp), net::use_awaitable);
        }
        else
        {
            // 序列化发送
            const auto data = http::serialize(req, &pool_);
            co_await net::async_write(*upstream_, net::buffer(data), net::use_awaitable);
        }

        co_await tunnel_tcp();
    }

    /**
     * @brief 隧道 TCP 流量
     * @details 该函数会在客户端套接字和上游服务器套接字之间建立隧道，实现流量的双向传输。
     */
    template<socket_concept Transport>
    net::awaitable<void> session<Transport>::tunnel_tcp()
    {
        if (!upstream_)
        {
            co_return;
        }

        auto stop_once = std::make_shared<std::atomic_bool>(false);
        auto stop_all = [self = this->shared_from_this(), stop_once]() -> net::awaitable<void>
        {
            if (stop_once->exchange(true))
            {
                co_return;
            }

            boost::system::error_code ec;
            if constexpr (requires { self->client_socket_.is_open(); })
            {
                if (self->client_socket_.is_open())
                {
                    self->client_socket_.shutdown(tcp::socket::shutdown_both, ec);
                    self->client_socket_.close(ec);
                }
            }

            if (self->upstream_)
            {
                self->upstream_->shutdown(tcp::socket::shutdown_both, ec);
                self->upstream_->close(ec);
            }

            co_return;
        };

        auto left_to_right = net::co_spawn(io_context_,
                                           [self = this->shared_from_this(), stop_all]() -> net::awaitable<void>
                                           {
                                               co_await self->transfer_tcp(self->client_socket_, *self->upstream_);
                                               co_await stop_all();
                                           },
                                           net::use_awaitable);

        auto right_to_left = net::co_spawn(io_context_,
                                           [self = this->shared_from_this(), stop_all]() -> net::awaitable<void>
                                           {
                                               co_await self->transfer_tcp(*self->upstream_, self->client_socket_);
                                               co_await stop_all();
                                           },
                                           net::use_awaitable);

        co_await std::move(left_to_right);
        co_await std::move(right_to_left);
        upstream_.reset();
    }

    /**
     * @brief 处理 obscura 协议
     * @details 该函数会处理 obscura 协议，包括握手、数据传输等。
     */
    template <socket_concept Transport>
    net::awaitable<void> session<Transport>::handle_obscura()
    {
        if (!ssl_ctx_)
        {
            co_return;
        }

        pool_.release();
        auto proto = std::make_shared<obscura<tcp>>(std::move(client_socket_), ssl_ctx_, role::server);
        std::string target_path = co_await proto->handshake();
        if (target_path.starts_with('/'))
        {
            target_path.erase(0, 1);
        }
        const auto target = analysis::resolve(std::string_view(target_path), &pool_);
        if (target.host.empty())
        {
            co_return;
        }

        upstream_ = co_await distributor_.route_forward(target.host, target.port);
        if (!upstream_ || !upstream_->is_open())
        {
            co_return;
        }

        co_await tunnel_obscura(std::move(proto));
    }

    /**
     * @brief 从 obscura 协议读取数据并写入服务器
     * @param proto obscura 协议实例
     * @details 该函数会从 obscura 协议读取数据，并将数据写入服务器。
     */
    template <socket_concept Transport>
    net::awaitable<void> session<Transport>::transfer_obscura_from_proto(obscura<tcp> &proto) const
    {
        beast::flat_buffer buffer;
        try
        {
            while (true)
            {
                const std::size_t n = co_await proto.async_read(buffer);
                if (n == 0)
                {
                    break;
                }
                co_await net::async_write(*upstream_, buffer.data(), net::use_awaitable);
                buffer.consume(n);
            }
        }
        catch (...)
        {
        }
    }

    /**
     * @brief 从服务器读取数据并写入 obscura 协议
     * @param proto obscura 协议实例
     * @details 该函数会从服务器读取数据，并将数据写入 obscura 协议实例。
     */
    template <socket_concept Transport>
    net::awaitable<void> session<Transport>::transfer_obscura_to_proto(obscura<tcp> &proto) const
    {
        std::array<char, 8192> buf{};
        try
        {
            while (true)
            {
                const std::size_t n = co_await adaptation::async_read(*upstream_, net::buffer(buf), net::use_awaitable);
                if (n == 0)
                {
                    break;
                }
                co_await proto.async_write(std::string_view(buf.data(), n));
            }
        }
        catch (...)
        {
        }
    }

    /**
    * @brief 隧道 obscura 协议
    * @param proto obscura 协议实例
    * @details 该函数会在客户端和服务器之间建立一个隧道，将 obscura 协议的数据进行加密传输。
    */
    template <socket_concept Transport>
    net::awaitable<void> session<Transport>::tunnel_obscura(std::shared_ptr<obscura<tcp>> proto)
    {
        if (!proto)
        {
            co_return;
        }

        auto stop_once = std::make_shared<std::atomic_bool>(false);
        auto stop_all = [self = this->shared_from_this(), proto, stop_once]() -> net::awaitable<void>
        {
            if (stop_once->exchange(true))
            {
                co_return;
            }

            boost::system::error_code ec;
            if (self->upstream_)
            {
                self->upstream_->shutdown(tcp::socket::shutdown_both, ec);
                self->upstream_->close(ec);
            }

            try
            {
                co_await proto->close();
            }
            catch (...)
            {
            }

            co_return;
        };

        auto obscura_to_upstream = net::co_spawn(io_context_,
                                                 [self = this->shared_from_this(), proto, stop_all]() -> net::awaitable<void>
                                                 {
                                                     co_await self->transfer_obscura_from_proto(*proto);
                                                     co_await stop_all();
                                                 },
                                                 net::use_awaitable);

        auto upstream_to_obscura = net::co_spawn(io_context_,
                                                 [self = this->shared_from_this(), proto, stop_all]() -> net::awaitable<void>
                                                 {
                                                     co_await self->transfer_obscura_to_proto(*proto);
                                                     co_await stop_all();
                                                 },
                                                 net::use_awaitable);

        co_await std::move(obscura_to_upstream);
        co_await std::move(upstream_to_obscura);
        upstream_.reset();
    }
}
