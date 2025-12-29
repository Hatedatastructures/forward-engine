#pragma once

#include <memory>
#include <utility>
#include <string>
#include <string_view>
#include <array>
#include <cctype>
#include <charconv>
#include <atomic>
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

        [[nodiscard]] net::awaitable<bool> read_http_request(http::request &out_req);
        [[nodiscard]] static std::string_view trim_ascii_space(std::string_view value);

        net::awaitable<void> diversion();
        net::awaitable<void> tunnel_tcp();
        net::awaitable<void> handle_http();
        net::awaitable<void> handle_obscura();
        net::awaitable<void> tunnel_obscura(std::shared_ptr<obscura<tcp>> proto);
        net::awaitable<void> transfer_obscura_to_proto(obscura<tcp> &proto) const;
        net::awaitable<void> transfer_obscura_from_proto(obscura<tcp> &proto) const;

        static bool equals_ignore(std::string_view left, std::string_view right);
        static bool parse_content_length(std::string_view headers_block, std::uint64_t &out_length);
        static bool has_chunked_transfer_encoding(std::string_view headers_block);

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
    }; // class session
}

namespace ngx::agent
{
    template <socket_concept Transport>
    session<Transport>::session(net::io_context &io_context, socket_type socket, distributor &dist,
        std::shared_ptr<ssl::context> ssl_ctx)
    : io_context_(io_context), ssl_ctx_(std::move(ssl_ctx)), distributor_(dist),
    client_socket_(std::move(socket)) {}

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
     * @brief 移除字符串首尾的 ASCII 空格字符
     * @param value 输入字符串
     * @return 移除空格后的字符串视图
     */
    template <socket_concept Transport>
    std::string_view session<Transport>::trim_ascii_space(std::string_view value)
    {
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        {
            value.remove_prefix(1);
        }

        while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
        {
            value.remove_suffix(1);
        }

        return value;
    }

    /**
     * @brief 忽略大小写比较两个字符串是否相等
     * @param left 左字符串
     * @param right 右字符串
     * @return 如果两个字符串忽略大小写相等，返回 true；否则返回 false
     */
    template <socket_concept Transport>
    bool session<Transport>::equals_ignore(const std::string_view left, const std::string_view right)
    {
        if (left.size() != right.size())
        {
            return false;
        }

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            const auto l = static_cast<unsigned char>(left[i]);
            const auto r = static_cast<unsigned char>(right[i]);
            if (std::tolower(l) != std::tolower(r))
            {
                return false;
            }
        }

        return true;
    }

    /**
     * @brief 解析 Content-Length 头字段
     * @param headers_block HTTP 头块
     * @param out_length 解析出的内容长度
     * @return 如果解析成功，返回 true；否则返回 false
     */
    template <socket_concept Transport>
    bool session<Transport>::parse_content_length(const std::string_view headers_block, std::uint64_t &out_length)
    {
        std::size_t offset = 0;
        while (offset < headers_block.size())
        {
            const std::size_t line_end = headers_block.find("\r\n", offset);
            const std::size_t end = (line_end == std::string_view::npos) ? headers_block.size() : line_end;
            const std::string_view line = headers_block.substr(offset, end - offset);
            offset = (line_end == std::string_view::npos) ? headers_block.size() : (line_end + 2);

            if (line.empty())
            {
                continue;
            }

            const std::size_t colon_pos = line.find(':');
            if (colon_pos == std::string_view::npos)
            {
                continue;
            }

            const std::string_view name = trim_ascii_space(line.substr(0, colon_pos));
            if (!equals_ignore(name, "Content-Length"))
            {
                continue;
            }

            const std::string_view value = trim_ascii_space(line.substr(colon_pos + 1));
            std::uint64_t length_value = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), length_value);
            if (result.ec != std::errc{})
            {
                return false;
            }

            out_length = length_value;
            return true;
        }

        return false;
    }

    /**
     * @brief 检查是否存在分块传输编码
     * @param headers_block HTTP 头块
     * @return 如果存在分块传输编码，返回 true；否则返回 false
     */
    template <socket_concept Transport>
    bool session<Transport>::has_chunked_transfer_encoding(const std::string_view headers_block)
    {
        std::size_t offset = 0;
        while (offset < headers_block.size())
        {
            const std::size_t line_end = headers_block.find("\r\n", offset);
            const std::size_t end = (line_end == std::string_view::npos) ? headers_block.size() : line_end;
            const std::string_view line = headers_block.substr(offset, end - offset);
            offset = (line_end == std::string_view::npos) ? headers_block.size() : (line_end + 2);

            if (line.empty())
            {
                continue;
            }

            const std::size_t colon_pos = line.find(':');
            if (colon_pos == std::string_view::npos)
            {
                continue;
            }

            const std::string_view name = trim_ascii_space(line.substr(0, colon_pos));
            if (!equals_ignore(name, "Transfer-Encoding"))
            {
                continue;
            }

            const std::string_view value = trim_ascii_space(line.substr(colon_pos + 1));
            std::size_t value_offset = 0;
            while (value_offset < value.size())
            {
                const std::size_t comma_pos = value.find(',', value_offset);
                const std::size_t token_end = (comma_pos == std::string_view::npos) ? value.size() : comma_pos;
                const std::string_view token = trim_ascii_space(value.substr(value_offset, token_end - value_offset));
                if (!token.empty() && equals_ignore(token, "chunked"))
                {
                    return true;
                }
                value_offset = (comma_pos == std::string_view::npos) ? value.size() : (comma_pos + 1);
            }
            return false;
        }

        return false;
    }

    /**
     * @brief 读取 HTTP 请求
     * @details 该函数会从客户端套接字中读取 HTTP 请求，直到完整读取或发生错误。
     * @return 读取到的 HTTP 请求字符串
     */
    template <socket_concept Transport>
    net::awaitable<bool> session<Transport>::read_http_request(http::request &out_req)
    {
        out_req.clear();
        try
        {
            beast::flat_buffer buffer;

            beast::http::request_parser<beast::http::string_body> parser;
            parser.body_limit(1024 * 1024);
            co_await beast::http::async_read(client_socket_, buffer, parser, net::use_awaitable);

            const auto &beast_req = parser.get();

            out_req.method(beast_req.method_string());
            out_req.target(beast_req.target());
            out_req.version(beast_req.version());

            for (const auto &field : beast_req)
            {
                out_req.set(field.name_string(), field.value());
            }

            if (!beast_req.body().empty())
            {
                out_req.erase("Transfer-Encoding");
                out_req.body(beast_req.body());
            }

            out_req.keep_alive(beast_req.keep_alive());
            co_return true;
        }
        catch (...)
        {
            out_req.clear();
            co_return false;
        }
    }

    /**
     * @brief 处理HTTP请求
     * @details 该函数会从客户端读取HTTP请求，并根据请求类型进行相应的处理。
     */
    template <socket_concept Transport>
    net::awaitable<void> session<Transport>::handle_http()
    {
        http::request req;
        const bool success = co_await http::async_read(client_socket_, req);

        if (!success)
        {
            co_return;
        }


        //  连接上游
        if (const auto [host, port, forward_proxy] = analysis::resolve(req); forward_proxy)
        {
            upstream_ = co_await distributor_.route_forward(host, port);
        } 
        else 
        {
            upstream_ = co_await distributor_.route_reverse(host);
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
            std::string data = http::serialize(req);
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

        auto proto = std::make_shared<obscura<tcp>>(std::move(client_socket_), ssl_ctx_, role::server);
        std::string target_path = co_await proto->handshake();
        if (target_path.starts_with('/'))
        {
            target_path.erase(0, 1);
        }
        const auto target = analysis::resolve(std::string_view(target_path));
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
