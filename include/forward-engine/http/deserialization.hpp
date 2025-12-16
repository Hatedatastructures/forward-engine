#pragma once

#include <string_view>

#include "request.hpp"
#include "response.hpp"

namespace ngx::http
{
    /**
     * @brief 反序列化 HTTP 请求
     * @param string_value 原始 `HTTP` 请求报文数据
     * @param request_instance 用于接收解析结果的 `request` 对象
     * @return bool 如果解析成功返回 `true`，否则返回 `false`
     */
    [[nodiscard]] bool deserialize(std::string_view string_value, request &request_instance);

    /**
     * @brief 反序列化 HTTP 响应
     * @param string_value 原始 `HTTP` 响应报文数据
     * @param response_instance 用于接收解析结果的 `response` 对象
     * @return bool 如果解析成功返回 `true`，否则返回 `false`
     */
    [[nodiscard]] bool deserialize(std::string_view string_value, response &response_instance);
} // namespace ngx::http
