#include <agent/analysis.hpp>
#include <array>



namespace ngx::agent
{
    /**
     * @brief 通过预读的数据判断协议类型
     * @note 由于 obscura 没有提供静态检测方法，我们采用“白名单检测法”：
     * 只要看起来像 HTTP，就是 HTTP；否则认为是 Obscura/自定义协议。
     */
    protocol_type analysis::detect(const std::string_view peek_data)
    {
        // HTTP 方法列表 (最短的 3 字节 GET/PUT)
        static constexpr std::array<std::string_view, 9> http_methods =
        {
            "GET ", "POST ", "HEAD ", "PUT ", "DELETE ",
            "CONNECT ", "OPTIONS ", "TRACE ", "PATCH "};

        if (peek_data.size() < 4)
            return protocol_type::unknown;

        for (const auto &method : http_methods)
        {
            if (peek_data.size() >= method.size() && peek_data.substr(0, method.size()) == method)
            {
                return protocol_type::http;
            }
        }

        // 2. 特殊处理：如果第一个字节是 0x16 (TLS Handshake)，且不是 HTTP
        // 这可能是 HTTPS 正向代理（无 CONNECT 直接发 TLS 的情况很少见，通常都有 CONNECT）
        // 目前的策略：不是 HTTP -> Obscura
        return protocol_type::obscura;
    }

    /**
     * @brief 解析 "host:port" 格式的字符串
     * @param src 输入字符串，格式为 "host:port" 或 "host"
     * @param host 输出参数，解析出的主机名
     * @param port 输出参数，解析出的端口号（默认 80）
     */
    void analysis::serialization(std::string_view src, std::string &host, std::string &port)
    {
        if (const auto pos = src.find(':'); pos != std::string_view::npos)
        {
            host = std::string(src.substr(0, pos));
            port = std::string(src.substr(pos + 1));
        }
        else
        {
            host = std::string(src);
        }
    }
}