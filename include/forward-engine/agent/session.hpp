#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <chrono>
#include <boost/asio.hpp>
#include "obscura.hpp"

namespace ngx::agent
{
    using tcp = boost::asio::ip::tcp;
    /**
     * @brief 会话管理类
     * @tparam protocol 协议类型，必须满足 protocol_concept 约束
     * @note 负责管理与目标服务器（或客户端）的连接，提供超时控制和优雅关闭功能。
     */
    template <protocol_concept protocol>
    class session : public std::enable_shared_from_this<session<protocol>>
    {
    public:
        /**
         * @brief 构造函数
         * @param io_context IO 上下文
         */
        explicit session(net::io_context &io_context)
            : timer_(io_context), io_context_(io_context)
        {
        }

        virtual ~session()
        {
            close();
        }

        /**
         * @brief 设置 socket (移动语义)
         * @param socket 协议 socket
         */
        void socket(protocol::socket &&socket)
        {
            socket_ = std::make_shared<typename protocol::socket>(std::move(socket));
            update_remote_info();
        }

        /**
         * @brief 设置 socket (共享指针)
         * @param socket 协议 socket 共享指针
         */
        void socket(std::shared_ptr<typename protocol::socket> socket)
        {
            socket_ = socket;
            update_remote_info();
        }

        /**
         * @brief 获取 socket
         * @return socket 共享指针引用
         */
        [[nodiscard]] std::shared_ptr<typename protocol::socket> &socket()
        {
            return socket_;
        }

        /**
         * @brief 异步读取数据
         * @param data 用于存储读取数据的字符串
         * @return 读取的字节数
         */
        net::awaitable<std::size_t> async_read(std::string &data)
        {
            refresh_timeout();

            try
            {
                if (!socket_)
                    co_return 0;

                std::size_t n = 0;
                // 使用 8KB 缓冲区
                char buffer[8192];
                auto buffer_view = net::buffer(buffer);

                n = co_await socket_->async_read_some(buffer_view, net::use_awaitable);

                if (n > 0)
                {
                    data.assign(buffer, n);
                }

                // 读取成功，刷新超时
                refresh_timeout();
                co_return n;
            }
            catch (const boost::system::system_error &e)
            {
                if (e.code() == net::error::eof)
                {
                    co_return 0;
                }
                throw;
            }
        }

        /**
         * @brief 异步写入数据
         * @param data 要写入的数据
         * @return 写入的字节数
         */
        net::awaitable<std::size_t> async_write(std::string_view data)
        {
            refresh_timeout();
            try
            {
                if (!socket_)
                    co_return 0;

                std::size_t n = 0;
                n = co_await net::async_write(*socket_, net::buffer(data), net::use_awaitable);

                // 写入成功，刷新超时
                refresh_timeout();
                co_return n;
            }
            catch (...)
            {
                throw;
            }
        }

        /**
         * @brief 异步连接
         * @param endpoint 目标端点
         */
        net::awaitable<void> async_connect(const typename protocol::endpoint &endpoint)
        {
            // 连接超时定时器 (5秒)
            net::steady_timer connect_timer(io_context_);
            connect_timer.expires_after(std::chrono::seconds(5));

            auto wait = [weak = this->weak_from_this()](const boost::system::error_code &ec)
            {
                if (!ec)
                {
                    // 超时触发
                    if (auto self = weak.lock())
                    {
                        self->close();
                    }
                }
            };

            connect_timer.async_wait(wait);

            try
            {
                if (!socket_)
                {
                    socket_ = std::make_shared<typename protocol::socket>(io_context_);
                }

                co_await socket_->async_connect(endpoint, net::use_awaitable);

                connect_timer.cancel();
                update_remote_info();

                refresh_timeout();
            }
            catch (...)
            {
                connect_timer.cancel();
                throw;
            }
        }

        /**
         * @brief 获取远程地址
         */
        [[nodiscard]] std::string remote_address() const
        {
            return remote_address_;
        }

        /**
         * @brief 获取远程端口
         */
        [[nodiscard]] std::uint16_t remote_port() const
        {
            return remote_port_;
        }

        /**
         * @brief 关闭连接
         */
        void close()
        {
            if (socket_ && socket_->is_open())
            {
                boost::system::error_code ec;
                socket_->close(ec);
            }
            // 取消超时定时器
            timer_.cancel();
        }

    private:
        /**
         * @brief 更新远程连接信息
         */
        void update_remote_info()
        {
            if (socket_ && socket_->is_open())
            {
                boost::system::error_code ec;
                auto ep = socket_->remote_endpoint(ec);
                if (!ec)
                {
                    remote_address_ = ep.address().to_string();
                    remote_port_ = ep.port();
                }
            }
        }

        /**
         * @brief 刷新空闲超时定时器 (60秒)
         */
        void refresh_timeout()
        {
            // 设置新的超时时间，这会取消之前的 async_wait
            timer_.expires_after(std::chrono::seconds(60));

            auto wait = [weak = this->weak_from_this()](const boost::system::error_code &ec)
            {
                if (!ec)
                {
                    // 如果错误码为 0，说明定时器真的触发了（未被取消）
                    if (auto self = weak.lock())
                    {
                        self->close();
                    }
                }
            };

            // 重新挂载 async_wait
            timer_.async_wait(wait);
        }

        std::string remote_address_;
        std::uint16_t remote_port_{0};
        net::steady_timer timer_;
        net::io_context &io_context_;

        // Protocol Socket
        std::shared_ptr<typename protocol::socket> socket_;
    };
}
