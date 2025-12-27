#pragma once

#include <memory>
#include <type_traits>
#include <utility>
#include <string>
#include <string_view>
#include <chrono>
#include <boost/asio.hpp>
#include "obscura.hpp"
#include "connection.hpp"
#include "adaptation.hpp"
#include <memory/pointer.hpp>

namespace ngx::agent
{
    using tcp = boost::asio::ip::tcp;

    
    /**
     * @brief 会话管理类
     * @tparam socket socket 类型，必须满足 socket_concept 约束
     * @note 负责管理与目标服务器（或客户端）的连接，提供超时控制和优雅关闭功能。
     *       已解耦具体 protocol，直接操作 socket。
     */
    template <socket_concept socket>
    class session : public std::enable_shared_from_this<session<socket>>
    {
    public:
        // 导出 Socket 类型和 Endpoint 类型
        using socket_type = socket;
        using endpoint_type = typename socket::endpoint_type;
        using socket_ptr_type = std::conditional_t<std::is_same_v<socket_type, tcp::socket>, internal_ptr, std::unique_ptr<socket_type>>;

        /**
         * @brief 构造函数
         * @param io_context IO 上下文
         */
        explicit session(net::io_context &io_context, socket_type socket, distributor &dist)
            : io_context_(io_context),
              distributor_(dist),
              client_socket_(std::move(socket)) // 1. 接管客户端连接 (左手)
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
        void socket(socket_type &&socket)
        {
            if constexpr (std::is_same_v<socket_type, tcp::socket>)
            {
                socket_ = socket_ptr_type(new socket_type(std::move(socket)), deleter{});
            }
            else
            {
                socket_ = std::make_unique<socket_type>(std::move(socket));
            }
            update_remote_info();
        }

        /**
         * @brief 设置 socket (内部指针)
         * @param socket 协议 socket 内部指针
         */
        void socket(socket_ptr_type socket)
        {
            socket_ = std::move(socket);
            update_remote_info();
        }

        /**
         * @brief 获取 socket
         * @return socket 原始指针
         */
        [[nodiscard]] socket_type *socket()
        {
            return socket_.get();
        }

        /**
         * @brief 获取 socket
         * @return socket 原始指针
         */
        [[nodiscard]] const socket_type *socket() const
        {
            return socket_.get();
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

                // 使用 adaptation 适配器调用读取
                n = co_await adaptation::async_read(*socket_, buffer_view, net::use_awaitable);

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
        net::awaitable<std::size_t> async_write(const std::string_view data)
        {
            refresh_timeout();
            try
            {
                if (!socket_)
                    co_return 0;

                std::size_t n = 0;
                // 使用 adaptation 适配器调用写入
                n = co_await adaptation::async_write(*socket_, net::buffer(data), net::use_awaitable);

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
        net::awaitable<void> async_connect(const endpoint_type &endpoint)
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
                    if constexpr (std::is_same_v<socket_type, tcp::socket>)
                    {
                        socket_ = socket_ptr_type(new socket_type(io_context_), deleter{});
                    }
                    else
                    {
                        socket_ = std::make_unique<socket_type>(io_context_);
                    }
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

        void start()
        {
            // 启动协程，并使用 shared_from_this() 自保，防止 Session 在协程结束前被析构
            auto process = [self = this->shared_from_this()]() -> net::awaitable<void>
            {
                co_await self->run_process();
            };
            net::co_spawn(io_context_,process,net::detached);
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
         * @brief 释放 socket 所有权
         * @details 将 socket 的所有权从 session 中转移出来，防止 session 析构时关闭 socket。
         *          通常用于将连接归还给连接池复用。
         *          调用此函数后，session 将不再持有 socket，且会取消超时定时器。
         *          注意：调用此函数前必须确保 socket 处于空闲状态（无挂起操作），否则可能会导致未定义行为。
         * @return socket 内部指针
         */
        [[nodiscard]] socket_ptr_type release_socket()
        {
            // 1. 取消超时定时器
            timer_.cancel();

            // 2. 转移所有权，socket_ 变为 nullptr
            // 此时 session 析构将不会关闭 socket，因为 socket_ 已为空
            return std::move(socket_);
        }

        /**
         * @brief 强制关闭会话
         */
        void close()
        {
            boost::system::error_code ec;
            // 关闭客户端连接
            if constexpr (requires { client_socket_.is_open(); }) 
            {
                if (client_socket_.is_open()) 
                {
                    client_socket_.close(ec);
                }
            }
            // upstream_ 是 unique_ptr，会自动归还给连接池，无需手动操作
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
         * @brief 核心业务逻辑：解析 -> 路由 -> 转发
         */
        net::awaitable<void> run_process()
        {
            try
            {
                // 1. 读取 HTTP 头部 (从客户端读)
                // 注意：这里需要你的 http::request 类支持从 socket 引用读取
                http::request req;
                // 如果 request.hpp 还没适配模板 socket，你需要确保它能接受 client_socket_
                co_await req.async_read_header(client_socket_);

                // 2. 路由决策 (问 Distributor 要连接)
                if (req.is_forward_proxy()) 
                {
                    // ---> 正向代理 (去 Google 等)
                    std::string host = req.host();
                    std::string port = req.port();
                    // 这里会进行 DNS 解析、黑名单检查、连接池获取
                    upstream_ = co_await distributor_.route_forward(host, port);
                }
                else
                {
                    // ---> 反向代理 (去内部后端)
                    std::string host = req.host();
                    // 这里会查配置表、负载均衡
                    upstream_ = co_await distributor_.route_reverse(host);
                }

                // 3. 建立连接后的握手处理
                if (req.method() == "CONNECT")
                {
                    // HTTPS 隧道：先回复 200 Connection Established
                    const std::string resp = "HTTP/1.1 200 Connection Established\r\n\r\n";
                    co_await adaptation::async_write(client_socket_, net::buffer(resp), net::use_awaitable);
                }
                else
                {
                    // 普通 HTTP/WebSocket：
                    // 我们刚才读掉了 Header，现在要把它转发给上游，否则上游不知道你要干嘛
                    std::string raw_header = req.serialize(); 
                    co_await net::async_write(*upstream_, net::buffer(raw_header), net::use_awaitable);
                }

                // 4. 开始双向转发 (WebSocket 也会在这里自动处理)
                co_await tunnel();

            }
            catch (const std::exception &e)
            {
                // 任何环节出错（DNS失败、黑名单拦截、连接断开），关闭会话
                close();
            }
        }
        /**
         * @brief 双向隧道 (桥接 client 和 upstream)
         */
        net::awaitable<void> tunnel()
        {
            // 同时启动两个方向的数据搬运
            // C++20 协程的 && 操作符会等待两个协程都完成，或者任意一个抛出异常
            co_await (transfer(client_socket_, upstream_.get()) && transfer(upstream_.get(), client_socket_));
        }

        /**
         * @brief 单向数据搬运 (通用模板)
         */
        template <typename Source, typename Dest>
        net::awaitable<void> transfer(Source &from, Dest &to)
        {
            std::array<char, 8192> buf;
            try
            {
                while (true)
                {
                    // 读
                    auto n = co_await adaptation::async_read(from, net::buffer(buf), net::use_awaitable);
                    // 写
                    co_await adaptation::async_write(to, net::buffer(buf, n), net::use_awaitable);
                }
            }
            catch (...)
            {
                // 读写异常通常意味着连接关闭，正常退出协程即可
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
        distributor &distributor_; // 引用 Worker 的分发器

        // 【左手】客户端连接 (Session 独占，随 Session 销毁)
        socket_type client_socket_;

        // 【右手】上游连接 (从连接池借来，unique_ptr 会自动归还)
        // internal_ptr 定义在 connection.hpp (std::unique_ptr<tcp::socket, deleter>)
        internal_ptr upstream_
    }; // class session
}
