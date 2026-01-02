#pragma once

#include <memory>
#include <utility>
#include <string>
#include <string_view>
#include <array>
#include <cstddef>
#include <cctype>
#include <memory_resource>
#include <boost/asio.hpp>
#include <abnormal.hpp>
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
        using cslot = net::cancellation_slot;
        using mutable_buf = net::mutable_buffer;

        /**
         * @brief 判断 `boost::system::error_code` 是否属于“正常收尾”
         * @details “正常收尾”包括：对端正常关闭、被取消、连接被重置等。
         * 这些场景不应当转为业务异常抛出，否则会把正常断开误判为错误。
         */
        [[nodiscard]] static bool graceful(const boost::system::error_code &ec) noexcept
        {
            using namespace boost::asio;
            return ec == error::eof  || ec == error::operation_aborted || ec == error::connection_reset
                || ec == error::connection_aborted || ec == error::broken_pipe || ec == error::not_connected;
        }

        /**
         * @brief 预留的日志接口
         * @details 当前 `session` 仅负责捕获并上抛/汇总异常；真正的日志系统由更高层注入。
         */
        static void session_log(const std::string_view message) noexcept
        {
            static_cast<void>(message);
        }

        template <typename Socket>
        static void shut_close(Socket &socket) noexcept
        {
            if constexpr (requires { socket.is_open(); })
            {
                if (socket.is_open())
                {
                    boost::system::error_code ec;
                    if constexpr (requires { socket.shutdown(tcp::socket::shutdown_both, ec); })
                    {
                        socket.shutdown(tcp::socket::shutdown_both, ec);
                    }
                    socket.close(ec);
                }
            }
        }

        static void shut_close(const internal_ptr &socket_ptr) noexcept
        {
            if (socket_ptr)
            {
                shut_close(*socket_ptr);
            }
        }

        net::awaitable<void> diversion();
        net::awaitable<void> tunnel();

        net::awaitable<void> handle_http();
        net::awaitable<void> handle_obscura();

        net::awaitable<void> tunnel_obscura(std::shared_ptr<obscura<tcp>> proto);
        net::awaitable<void> transfer_obscura(obscura<tcp> &proto, cslot cancel_slot) const;
        net::awaitable<void> transfer_obscura(obscura<tcp> &proto, cslot cancel_slot, mutable_buf buffer) const;
        
        /**
         * @brief 从源读取数据并写入目标
         * @param from 源
         * @param to 目标
         * @param cancel_slot 取消信号槽实例
         * @param buffer 复用缓冲区（必须在 `co_await` 生命周期内保持有效）
         * @details 该函数会从源读取数据，并将数据写入目标。
         * 该函数不会使用 `close()` 来中断读写，而是依赖 `cancellation_slot` 触发的优雅取消。
         */
        template <typename Source, typename Dest>
        net::awaitable<void> transfer_tcp(Source &from, Dest &to, cslot cancel_slot, mutable_buf buffer)
        {
            boost::system::error_code ec;
            auto token = net::bind_cancellation_slot(cancel_slot, net::redirect_error(net::use_awaitable, ec));

            while (true)
            {
                ec.clear();
                const std::size_t n = co_await adaptation::async_read(from, buffer, token);
                if (ec)
                {
                    if (graceful(ec))
                    {
                        co_return;
                    }
                    throw abnormal::network_error("transfer_tcp 读失败: {}", ec.message());
                }

                if (n == 0)
                {
                    co_return;
                }

                ec.clear();
                co_await adaptation::async_write(to, net::buffer(buffer.data(), n), token);
                if (ec)
                {
                    if (graceful(ec))
                    {
                        co_return;
                    }
                    throw abnormal::network_error("transfer_tcp 写失败: {}", ec.message());
                }
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
        auto completion = [self = this->shared_from_this()](const std::exception_ptr &ep) noexcept
        {
            if (!ep)
            {
                return;
            }

            try
            {
                std::rethrow_exception(ep);
            }
            catch (const abnormal::exception &e)
            {
                session_log(e.dump());
            }
            catch (const std::exception &e)
            {
                session_log(e.what());
            }

            self->close();
        };

        net::co_spawn(io_context_, std::move(process), std::move(completion));
    }

    /**
     * @brief 关闭会话
     * @details 该函数会关闭与目标服务器（或客户端）的连接，释放相关资源。
     */
    template <socket_concept Transport>
    void session<Transport>::close()
    {
        shut_close(client_socket_);
        shut_close(upstream_);
        upstream_.reset();
    }

    /**
     * @brief 会话分发器
     * @details 该函数会根据请求协议类型，选择相应的处理函数。
     */
    template <socket_concept Transport>
    net::awaitable<void> session<Transport>::diversion()
    {
        boost::system::error_code ec;

        // 1. 偷看数据 (Peek)
        std::array<char, 24> peek_buf{};
        auto buf = net::buffer(peek_buf);
        auto token = net::redirect_error(net::use_awaitable, ec);
        const std::size_t n = co_await client_socket_.async_receive(buf, tcp::socket::message_peek, token);
        if (ec)
        {
            if (graceful(ec))
            {
                co_return;
            }
            throw abnormal::network_error("diversion peek 失败: {}", ec.message());
        }

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
            boost::system::error_code ec;
            auto token = net::redirect_error(net::use_awaitable, ec);
            const std::string resp = "HTTP/1.1 200 Connection Established\r\n\r\n";
            co_await net::async_write(client_socket_, net::buffer(resp), token);
            if (ec && !graceful(ec))
            {
                throw abnormal::network_error("CONNECT 响应发送失败: {}", ec.message());
            }
        }
        else
        {
            // 序列化发送
            const auto data = http::serialize(req, &pool_);
            boost::system::error_code ec;
            auto token = net::redirect_error(net::use_awaitable, ec);
            co_await net::async_write(*upstream_, net::buffer(data), token);
            if (ec && !graceful(ec))
            {
                throw abnormal::network_error("HTTP 请求转发失败: {}", ec.message());
            }
        }

        co_await tunnel();
    }

    /**
     * @brief 隧道 TCP 流量
     * @details 该函数会在客户端套接字和上游服务器套接字之间建立隧道，实现流量的双向传输。
     */
    template<socket_concept Transport>
    net::awaitable<void> session<Transport>::tunnel()
    {
        if (!upstream_)
        {
            co_return;
        }

        /**
         * @details 关键改动：不再使用 `close()` 暴力打断另一个方向的 `co_await`。
         * 这里使用 `cslot` 给两个方向各自分配一个取消槽：
         * - `left_to_right` 方向结束（`EOF`/取消/错误）后，触发 `cleft` 方向的取消信号；
         * - `right_to_left` 方向同理触发 `cright` 方向。
         * 这样可以区分“正常断开”和“异常错误”，并避免强制关闭导致的误报。
         */
        pool_.release();

        auto executor = co_await net::this_coro::executor;

        cslot cleft,cright;

        const std::size_t half = buffer_.size() / 2;
        auto lbuf = mutable_buf(buffer_.data(), half);
        auto rbuf = mutable_buf(buffer_.data() + half, buffer_.size() - half);

        auto ltoken = [self = this->shared_from_this(),lslot = cleft.slot(),signal = &cright,lbuf]() 
            -> net::awaitable<void>
        {   // left_to_right
            try
            {
                co_await self->transfer_tcp(*self->upstream_, self->client_socket_, lslot, lbuf);
            }
            catch (...)
            {
                signal->emit(net::cancellation_type::all);
                throw;
            }
            signal->emit(net::cancellation_type::all);

        };

        auto rtoken = [self = this->shared_from_this(),rslot = cright.slot(),signal = &cleft,rbuf]() 
            -> net::awaitable<void>
        {   // right_to_left
            try
            {
                co_await self->transfer_tcp(*self->upstream_, self->client_socket_, rslot, rbuf);
            }
            catch (...)
            {
                signal->emit(net::cancellation_type::all);
                throw;
            }
            signal->emit(net::cancellation_type::all);
        };

        auto lfuture = net::co_spawn(executor,ltoken,net::use_awaitable);
        auto rfuture = net::co_spawn(executor,rtoken,net::use_awaitable);

        /**
         * @details 到这里两个任务已经并行启动了，下面等待它们完成，如果写在下面的 try-catch 中，
         * 则会导致先完成的任务抛出异常，而另一个任务继续运行，这不是期望的行为, try代码块中 move 的是一个可等待对象,
         * 它会在完成时抛出异常,如果我们不捕获异常,则会导致程序崩溃。
         */

        std::exception_ptr first_error; // 创建一个异常指针,用于存储第一个异常
        try
        {
            co_await std::move(lfuture);
        }
        catch (...)
        {
            first_error = std::current_exception();
        }

        try
        {
            co_await std::move(rfuture);
        }
        catch (...)
        {
            if (!first_error)
            {
                first_error = std::current_exception();
            }
        }

        shut_close(client_socket_);
        shut_close(upstream_);
        upstream_.reset();

        if (first_error)
        {   // 如果有异常,则重新抛出
            std::rethrow_exception(first_error);
        }
    }

    /**
     * @brief 处理 obscura 协议
     * @details 该函数会处理 obscura 协议，并建立与上游服务器的连接。
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
        std::string target_path;
        try
        {
            target_path = co_await proto->handshake();
        }
        catch (const boost::system::system_error &e)
        {
            throw abnormal::protocol_error("obscura 握手失败: {}", e.code().message());
        }
        catch (const std::exception &e)
        {
            throw abnormal::protocol_error("obscura 握手失败: {}", e.what());
        }

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
     * @param cancel_slot 取消信号槽实例
     * @details 该函数会从 obscura 协议读取数据，并将数据写入服务器。
     */
    template <socket_concept Transport>
    net::awaitable<void> session<Transport>::transfer_obscura(obscura<tcp> &proto, const cslot cancel_slot) const
    {
        beast::flat_buffer buffer;
        while (true)
        {
            std::size_t n = 0;
            try
            {
                n = co_await proto.async_read(buffer);
            }
            catch (const boost::system::system_error &e)
            {
                if (graceful(e.code()) || e.code() == beast::websocket::error::closed)
                {   // 正常关闭,不抛出异常
                    co_return;
                }
                throw abnormal::protocol_error("obscura 读失败: {}", e.code().message());
            }
            catch (const std::exception &e)
            {
                throw abnormal::protocol_error("obscura 读失败: {}", e.what());
            }

            if (n == 0)
            {
                co_return;
            }

            boost::system::error_code ec;
            auto token = net::bind_cancellation_slot(cancel_slot, net::redirect_error(net::use_awaitable, ec));
            co_await net::async_write(*upstream_, buffer.data(), token);
            if (ec)
            {
                if (graceful(ec))
                {
                    co_return;
                }
                throw abnormal::network_error("写入上游失败: {}", ec.message());
            }

            buffer.consume(n);
        }
    }

    /**
     * @brief 从服务器读取数据并写入 obscura 协议
     * @param proto obscura 协议实例
     * @param cancel_slot 取消信号槽实例
     * @param buffer 用于读取数据的缓冲区
     * @details 该函数会从服务器读取数据，并将数据写入 obscura 协议实例。
     */
    template <socket_concept Transport>
    net::awaitable<void> session<Transport>::transfer_obscura(obscura<tcp> &proto, const cslot cancel_slot, mutable_buf buffer) const
    {
        boost::system::error_code ec;
        auto token = net::bind_cancellation_slot(cancel_slot, net::redirect_error(net::use_awaitable, ec));

        while (true)
        {
            ec.clear();
            const std::size_t n = co_await adaptation::async_read(*upstream_, buffer, token);
            if (ec)
            {
                if (graceful(ec))
                {
                    co_return;
                }
                throw abnormal::network_error("从上游读取失败: {}", ec.message());
            }

            if (n == 0)
            {
                co_return;
            }

            try
            {   // 写入 obscura 协议
                const auto view = std::string_view(static_cast<const char *>(buffer.data()), n);
                co_await proto.async_write(view);
            }
            catch (const boost::system::system_error &e)
            {
                if (graceful(e.code()) || e.code() == beast::websocket::error::closed)
                {
                    co_return;
                }
                throw abnormal::protocol_error("obscura 写失败: {}", e.code().message());
            }
            catch (const std::exception &e)
            {
                throw abnormal::protocol_error("obscura 写失败: {}", e.what());
            }
        }
    }

    /**
     * @brief 建立 obscura 隧道,将 obscura 协议的数据进行加密传输
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

        pool_.release();
        // 获取当前协程的执行器
        auto executor = co_await net::this_coro::executor;

        net::cancellation_signal cancel_obscura_to_upstream;
        net::cancellation_signal cancel_upstream_to_obscura;

        const std::size_t half = buffer_.size() / 2;
        auto upstream_to_obscura_buffer = mutable_buf(buffer_.data(), half);

        auto outoken = [self = this->shared_from_this(), proto,slot = cancel_obscura_to_upstream.slot(),
            signal = &cancel_upstream_to_obscura]() -> net::awaitable<void>
        {
            try
            {
                co_await self->transfer_obscura(*proto, slot);
            }
            catch (...)
            {
                signal->emit(net::cancellation_type::all);
                throw;
            }
            signal->emit(net::cancellation_type::all);
        };

        auto uotoken = [self = this->shared_from_this(),proto,slot = cancel_upstream_to_obscura.slot(),
            signal = &cancel_obscura_to_upstream, upstream_to_obscura_buffer]() -> net::awaitable<void>
        {
            try
            {
                co_await self->transfer_obscura(*proto, slot, upstream_to_obscura_buffer);
            }
            catch (...)
            {
                signal->emit(net::cancellation_type::all);
                throw;
            }
            signal->emit(net::cancellation_type::all);
        };

        /**
         * @details co_spawn函数是调用就立即返回一个可等待对象，并把这个任务投递到io事件队列中运行
         */
        auto obscura_to_upstream = net::co_spawn(executor, outoken, net::use_awaitable);
        auto upstream_to_obscura = net::co_spawn(executor, uotoken, net::use_awaitable);


        /**
         * @details 和上面的函数逻辑同理
         */
        std::exception_ptr first_error;
        try
        {
            co_await std::move(obscura_to_upstream);
        }
        catch (...)
        {
            first_error = std::current_exception();
        }

        try
        {
            co_await std::move(upstream_to_obscura);
        }
        catch (...)
        {
            if (!first_error)
            {
                first_error = std::current_exception();
            }
        }

        try
        {
            co_await proto->close();
        }
        catch (const boost::system::system_error &e)
        {
            if (!graceful(e.code()) && e.code() != beast::websocket::error::closed)
            {
                if (!first_error)
                {
                    first_error = std::make_exception_ptr(abnormal::protocol_error("obscura 关闭失败: {}", e.code().message()));
                }
            }
        }
        catch (const std::exception &e)
        {
            if (!first_error)
            {
                first_error = std::make_exception_ptr(abnormal::protocol_error("obscura 关闭失败: {}", e.what()));
            }
        }

        shut_close(upstream_);
        upstream_.reset();

        if (first_error)
        {
            std::rethrow_exception(first_error);
        }
    }
}
