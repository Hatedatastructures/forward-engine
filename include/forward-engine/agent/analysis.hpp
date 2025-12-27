#pragma once

#include <string>
#include <string_view>

namespace ngx::agent
{
    /**
     * @brief 协议类型枚举
     * @note 协议类型包括 HTTP 和 Obscura（非 HTTP 流量）。
     */
    enum class protocol_type
    {
        unknown,
        http,
        obscura // 非 HTTP 流量都归为此类
    };

    /**
     * @brief 通过预读的数据判断协议类型
     * @note 由于 obscura 没有提供静态检测方法，我们采用“白名单检测法”：
     * 只要看起来像 HTTP，就是 HTTP；否则认为是 Obscura/自定义协议。
     */

    struct analysis
    {
        
        // 辅助结构：解析后的目标信息
        struct target
        {
            std::string host;
            std::string port = "80";
            bool is_forward_proxy = false;
        };

        static protocol_type detect(std::string_view peek_data);
        static void serialization(std::string_view src, std::string &host, std::string &port);
    }; // class analysis
}