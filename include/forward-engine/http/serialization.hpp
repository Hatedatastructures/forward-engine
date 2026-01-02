#pragma once

#include <string_view>
#include <memory_resource>
#include <memory/container.hpp>

#include "request.hpp"
#include "response.hpp"

namespace ngx::http
{
    /**
     * @brief 序列化 HTTP 请求
     * @param request_instance 要序列化的 `HTTP` 请求对象
     * @param mr 序列化结果分配所使用的内存资源
     * @return `memory::string` 序列化后的 `HTTP` 请求报文
     */
    [[nodiscard]] memory::string serialize(const request &request_instance, std::pmr::memory_resource *mr = std::pmr::get_default_resource());

    /**
     * @brief 序列化 HTTP 响应
     * @param response_instance 要序列化的 `HTTP` 响应对象
     * @param mr 序列化结果分配所使用的内存资源
     * @return `memory::string` 序列化后的 `HTTP` 响应报文
     */
    [[nodiscard]] memory::string serialize(const response &response_instance, std::pmr::memory_resource *mr = std::pmr::get_default_resource());
} // namespace ngx::http
