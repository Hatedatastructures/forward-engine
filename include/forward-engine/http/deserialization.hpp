#pragma once

#include <string_view>

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/core/flat_buffer.hpp>

#include "request.hpp"
#include "response.hpp"

namespace ngx::http
{
    namespace net = boost::asio;
    /**
     * @brief 反序列化 HTTP 请求
     * @param string_value 原始 `HTTP` 请求报文数据
     * @param request_instance 用于接收解析结果的 `request` 对象
     * @return bool 如果解析成功返回 `true`，否则返回 `false`
     */
    [[nodiscard]] bool deserialize(std::string_view string_value, request &request_instance);

    /**
     * @brief 异步读取并反序列化 HTTP 请求
     * @tparam Transport 支持异步反序列化的 Transport 类型 (tcp::socket 或 ssl::stream)
     * @param socket 数据源
     * @param request_instance http模块的 request 对象 (将被填充)
     * @return true 读取成功, false 读取失败 (连接断开或协议错误)
     */
    template <class Transport>
    net::awaitable<bool> async_read(Transport& socket, request& request_instance)
    {
        boost::beast::http::request_parser<boost::beast::http::string_body> parser;

        boost::beast::flat_buffer buffer;
        parser.header_limit(16 * 1024); 
        parser.body_limit(10 * 1024 * 1024);

        boost::system::error_code ec;
        auto token = net::redirect_error(net::use_awaitable, ec);
        co_await boost::beast::http::async_read(socket, buffer, parser, token);
        // 读取完成一个完整的请求窃取到自己的 request 对象
        if (ec)
        {
            co_return false;
        }

        // 这一步是“零拷贝”的关键，使用 std::move 转移所有权
        const auto& beast_msg = parser.get();

        // 窃取信息填充 request 对象
        request_instance.method(std::string(beast_msg.method_string()));
        request_instance.target(std::string(beast_msg.target()));
        request_instance.version(beast_msg.version());
        for (const auto& field : beast_msg)
        {
            request_instance.header().construct(std::string(field.name_string()), std::string(field.value()));
        }

        request_instance.body() = std::move(const_cast<std::string&>(beast_msg.body()));

        co_return true;
    }

    /**
     * @brief 反序列化 HTTP 响应
     * @param string_value 原始 `HTTP` 响应报文数据
     * @param response_instance 用于接收解析结果的 `response` 对象
     * @return bool 如果解析成功返回 `true`，否则返回 `false`
     */
    [[nodiscard]] bool deserialize(std::string_view string_value, response &response_instance);

    /**
     * @brief 异步读取并反序列化 HTTP 响应
     * @tparam Transport 支持异步反序列化的 Transport 类型 (tcp::socket 或 ssl::stream)
     * @param socket 数据源
     * @param response_instance http模块的 response 对象 (将被填充)
     * @return true 读取成功, false 读取失败 (连接断开或协议错误)
     */
    template <class Transport>
    net::awaitable<bool> async_read(Transport& socket, response& response_instance)
    {
        boost::beast::http::response_parser<boost::beast::http::string_body> parser;

        boost::beast::flat_buffer buffer;
        parser.header_limit(16 * 1024); 
        parser.body_limit(10 * 1024 * 1024);

        boost::system::error_code ec;
        auto token = net::redirect_error(net::use_awaitable, ec);
        co_await boost::beast::http::async_read(socket, buffer, parser, token);
        // 读取完成一个完整的响应窃取到自己的 response 对象
        if (ec)
        {
            co_return false;
        }

        // 这一步是“零拷贝”的关键，使用 std::move 转移所有权
        const auto& beast_msg = parser.get();

        // 窃取信息填充 response 对象
        response_instance.version(beast_msg.version());
        response_instance.status(static_cast<status>(beast_msg.result()));
        for (const auto& field : beast_msg)
        {
            response_instance.header().construct(std::string(field.name_string()), std::string(field.value()));
        }

        response_instance.body() = std::move(const_cast<std::string&>(beast_msg.body()));

        co_return true;
    }

} // namespace ngx::http
