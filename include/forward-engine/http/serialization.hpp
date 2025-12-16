#pragma once

#include <string>
#include <string_view>

#include "request.hpp"
#include "response.hpp"

namespace ngx::http
{
    /**
     * @brief 序列化 HTTP 请求
     * @param request_instance 要序列化的 `HTTP` 请求对象
     * @return std::string 序列化后的 `HTTP` 请求报文
     */
    [[nodiscard]] std::string serialize(const request &request_instance);

    /**
     * @brief 序列化 HTTP 响应
     * @param response_instance 要序列化的 `HTTP` 响应对象
     * @return std::string 序列化后的 `HTTP` 响应报文
     */
    [[nodiscard]] std::string serialize(const response &response_instance);
} // namespace ngx::http
