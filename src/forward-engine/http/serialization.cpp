#include <http/serialization.hpp>

namespace ngx::http
{
    namespace
    {
        /**
         * @brief 将 HTTP 版本号转换为字符串
         * @details 内部工具函数，用于将 `request` 或 `response` 中保存的
         * `11`、`10` 形式的版本号转换为 `1.1`、`1.0` 形式的字符串。
         * @param version_value 内部版本数值
         * @return std::string HTTP 版本数字字符串
         */
        [[nodiscard]] std::string to_version_string(const unsigned int version_value)
        {
            unsigned int major = version_value / 10;
            unsigned int minor = version_value % 10;

            std::string version_string;
            version_string.reserve(4);
            version_string.push_back(static_cast<char>('0' + static_cast<char>(major)));
            version_string.push_back('.');
            version_string.push_back(static_cast<char>('0' + static_cast<char>(minor)));

            return version_string;
        }

        /**
         * @brief 生成请求首行方法字符串
         * @details 优先使用 `request` 中缓存的 `method_string`，如果为空，则根据
         * 内部 `verb` 枚举值生成标准 `HTTP` 方法名字符串。
         * @param request_instance 请求对象实例
         * @return std::string_view 方法名字符串视图
         */
        [[nodiscard]] std::string_view resolve_request_method_string(const request &request_instance) noexcept
        {
            const std::string_view cached_method = request_instance.method_string();
            if (!cached_method.empty())
            {
                return cached_method;
            }

            switch (request_instance.method())
            {
            case verb::delete_:
                return "DELETE";
            case verb::get:
                return "GET";
            case verb::head:
                return "HEAD";
            case verb::post:
                return "POST";
            case verb::put:
                return "PUT";
            case verb::connect:
                return "CONNECT";
            case verb::options:
                return "OPTIONS";
            case verb::trace:
                return "TRACE";
            case verb::patch:
                return "PATCH";
            default:
                return "UNKNOWN";
            }
        }

        /**
         * @brief 解析响应原因短语
         * @details 优先使用 `response` 中显式设置的原因短语，如果为空，则根据
         * 状态码生成一个标准的 `HTTP` 原因短语。
         * @param response_instance 响应对象实例
         * @return std::string 响应原因短语字符串
         */
        [[nodiscard]] std::string resolve_response_reason(const response &response_instance)
        {
            const std::string_view reason_view = response_instance.reason();
            if (!reason_view.empty())
            {
                return std::string{reason_view};
            }

            switch (response_instance.status())
            {
            case status::ok:
                return "OK";
            case status::created:
                return "Created";
            case status::accepted:
                return "Accepted";
            case status::no_content:
                return "No Content";
            case status::moved_permanently:
                return "Moved Permanently";
            case status::found:
                return "Found";
            case status::see_other:
                return "See Other";
            case status::not_modified:
                return "Not Modified";
            case status::bad_request:
                return "Bad Request";
            case status::unauthorized:
                return "Unauthorized";
            case status::forbidden:
                return "Forbidden";
            case status::not_found:
                return "Not Found";
            case status::method_not_allowed:
                return "Method Not Allowed";
            case status::request_timeout:
                return "Request Timeout";
            case status::conflict:
                return "Conflict";
            case status::gone:
                return "Gone";
            case status::length_required:
                return "Length Required";
            case status::payload_too_large:
                return "Payload Too Large";
            case status::uri_too_long:
                return "URI Too Long";
            case status::unsupported_media_type:
                return "Unsupported Media Type";
            case status::range_not_satisfiable:
                return "Range Not Satisfiable";
            case status::expectation_failed:
                return "Expectation Failed";
            case status::upgrade_required:
                return "Upgrade Required";
            case status::too_many_requests:
                return "Too Many Requests";
            case status::internal_server_error:
                return "Internal Server Error";
            case status::not_implemented:
                return "Not Implemented";
            case status::bad_gateway:
                return "Bad Gateway";
            case status::service_unavailable:
                return "Service Unavailable";
            case status::gateway_timeout:
                return "Gateway Timeout";
            case status::http_version_not_supported:
                return "HTTP Version Not Supported";
            default:
                return {};
            }
        }
    } // namespace

    std::string serialize(const request &request_instance)
    {
        const std::string_view method_string = resolve_request_method_string(request_instance);
        const std::string &target_string = request_instance.target();
        const std::string version_string = to_version_string(request_instance.version());

        const headers &header_container = request_instance.header();
        const std::string_view body_view = request_instance.body();

        std::string result;
        result.reserve(128 + target_string.size() + static_cast<std::size_t>(header_container.size() * 32) + body_view.size());

        result.append(method_string.begin(), method_string.end());
        result.push_back(' ');
        result.append(target_string);
        result.append(" HTTP/");
        result.append(version_string);
        result.append("\r\n");

        for (const auto &header_entry : header_container)
        {
            if (header_entry.value.empty())
            {
                continue;
            }

            result.append(header_entry.original_key);
            result.append(": ");
            result.append(header_entry.value);
            result.append("\r\n");
        }

        result.append("\r\n");

        if (!body_view.empty())
        {
            result.append(body_view.begin(), body_view.end());
        }

        return result;
    }

    std::string serialize(const response &response_instance)
    {
        const std::string version_string = to_version_string(response_instance.version());
        const unsigned int status_code_value = response_instance.status_code();
        const std::string reason_string = resolve_response_reason(response_instance);

        const headers &header_container = response_instance.header();
        const std::string_view body_view = response_instance.body();

        char status_buffer[4]{};
        char *status_buffer_end = status_buffer; // 手动转换状态码性能更好
        {
            unsigned int code = status_code_value;
            status_buffer_end[2] = static_cast<char>('0' + static_cast<char>(code % 10));
            code /= 10;
            status_buffer_end[1] = static_cast<char>('0' + static_cast<char>(code % 10));
            code /= 10;
            status_buffer_end[0] = static_cast<char>('0' + static_cast<char>(code % 10));
            status_buffer_end += 3;
        }

        std::string result;
        result.reserve(128 + static_cast<std::size_t>(header_container.size() * 32) + body_view.size());

        result.append("HTTP/");
        result.append(version_string);
        result.push_back(' ');
        result.append(status_buffer, status_buffer_end);
        result.push_back(' ');
        result.append(reason_string);
        result.append("\r\n");

        for (const auto &header_entry : header_container)
        {
            if (header_entry.value.empty())
            {
                continue;
            }

            result.append(header_entry.original_key);
            result.append(": ");
            result.append(header_entry.value);
            result.append("\r\n");
        }

        result.append("\r\n");

        if (!body_view.empty())
        {
            result.append(body_view.begin(), body_view.end());
        }

        return result;
    }

} // namespace ngx::http
