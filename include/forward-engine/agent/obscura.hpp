#pragma once

#include <http.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>

namespace ngx::agent
{
    namespace net = boost::asio;
    namespace ssl = boost::asio::ssl;
    namespace beast = boost::beast;
    namespace bhttp = boost::beast::http;
    namespace websocket = beast::websocket;
    using tcp = boost::asio::ip::tcp;

    /**
     * @brief awaitable 概念
     * @tparam T awaitable 类型
     * @note awaitable 类型必须满足以下要求：
     * 
     * - 必须有 `value_type` 类型别名，且必须是 `std::size_t` 类型
     */
    template<typename T>
    concept awaitable_size = requires
    {
        typename T::value_type;   // awaitable 必有 value_type
        requires std::same_as<typename T::value_type, std::size_t>;
    };

    /**
     * @brief 协议概念
     * @tparam socket_type 协议类型
     * @note 协议类型必须满足以下要求：
     * 
     * - 必须有 `async_read_some` 成员函数，返回值必须是 `awaitable_size` 类型
     * 
     * - 必须有 `async_write_some` 成员函数，返回值必须是 `awaitable_size` 类型
     */
    template <typename socket_type>
    concept socket_concept = requires(socket_type &s, net::mutable_buffer mb, net::const_buffer cb)
    {
        { s.async_read_some(mb,  net::use_awaitable) } -> awaitable_size;
        { s.async_write_some(cb, net::use_awaitable) } -> awaitable_size;
    };

    /**
     * @brief 协议概念
     * @tparam protocol_type 协议类型
     * @note 协议类型必须满足以下要求：
     * 
     * - 必须有 `endpoint` 类型别名
     * 
     * - 必须有 `resolver` 类型别名
     * 
     * - 必须有 `socket` 类型别名，且必须是 `socket_concept` 类型
     */
    template <typename protocol_type>
    concept protocol_concept = requires
    {
       typename protocol_type::endpoint;
       typename protocol_type::resolver;
       requires socket_concept<typename protocol_type::socket>;
    };

    template <protocol_concept protocol>
    class obscura : public std::enable_shared_from_this<obscura<protocol>>
    {
        using ssl_request = beast::http::request<beast::http::empty_body>;
    public:
        using socket_type = typename protocol::socket;

        obscura(socket_type socket, net::ssl::context &ssl_context);
        obscura(const obscura&) = delete;
        obscura& operator=(const obscura&) = delete;

        net::awaitable<void> handshake(beast::flat_buffer& buffer, ssl_request& req);
    private:
        ssl::context &ssl_context;
        ssl::stream<socket_type> ssl_stream;
        websocket::stream<ssl::stream<socket_type>&> wsocket{ssl_stream};
    }; // class obscura

    template <protocol_concept protocol>
    obscura<protocol>::obscura(socket_type socket, net::ssl::context &ssl_context)
        : ssl_stream(std::move(socket), ssl_context), wsocket(ssl_stream)
    {}

    /**
     * @brief 握手
     * @param buffer 缓冲区
     * @param req_parser 请求解析器
     * @note  坑 ：流量特征
     */
    template <protocol_concept protocol>
    net::awaitable<void> obscura<protocol>::handshake(beast::flat_buffer& buffer, ssl_request& req)
    {
        // 先进行 SSL 握手，前提是设置证书，这个先进行非对称加密得到公钥和私钥 协商 对称加密算法 再进行对称加密
        co_await ssl_stream.async_handshake(ssl::stream_base::server,net::use_awaitable);
        co_await beast::http::async_read(ssl_stream, buffer, req, net::use_awaitable);
    }
} // namespace ngx::agent