#include <request.hpp>
#include <charconv>

namespace ngx::http
{
    namespace
    {
        [[nodiscard]] std::string_view to_string(const verb value) noexcept
        {
            switch (value)
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

        [[nodiscard]] verb string_to_verb(const std::string_view value) noexcept
        {
            if (value == "DELETE")
            {
                return verb::delete_;
            }
            if (value == "GET")
            {
                return verb::get;
            }
            if (value == "HEAD")
            {
                return verb::head;
            }
            if (value == "POST")
            {
                return verb::post;
            }
            if (value == "PUT")
            {
                return verb::put;
            }
            if (value == "CONNECT")
            {
                return verb::connect;
            }
            if (value == "OPTIONS")
            {
                return verb::options;
            }
            if (value == "TRACE")
            {
                return verb::trace;
            }
            if (value == "PATCH")
            {
                return verb::patch;
            }
            return verb::unknown;
        }

        [[nodiscard]] std::string_view to_string(const field value) noexcept
        {
            switch (value)
            {
            case field::host:
                return "Host";
            case field::user_agent:
                return "User-Agent";
            case field::connection:
                return "Connection";
            case field::accept:
                return "Accept";
            case field::accept_encoding:
                return "Accept-Encoding";
            case field::accept_language:
                return "Accept-Language";
            case field::content_length:
                return "Content-Length";
            case field::content_type:
                return "Content-Type";
            case field::transfer_encoding:
                return "Transfer-Encoding";
            default:
                return {};
            }
        }
    } // namespace

    void request::method(const verb method_value)
    {
        method_ = method_value;
        const std::string_view method_name = to_string(method_value);
        method_string_.assign(method_name.begin(), method_name.end());
    }

    verb request::method() const noexcept
    {
        return method_;
    }

    void request::method(const std::string_view method_value)
    {
        method_string_.assign(method_value.begin(), method_value.end());
        method_ = string_to_verb(method_value);
    }

    std::string_view request::method_string() const noexcept
    {
        return method_string_;
    }

    void request::target(const std::string_view target_value)
    {
        target_.assign(target_value.begin(), target_value.end());
    }

    const std::string &request::target() const noexcept
    {
        return target_;
    }

    void request::version(const unsigned int value)
    {
        version_ = value;
    }

    unsigned int request::version() const noexcept
    {
        return version_;
    }

    bool request::set(const std::string_view name, const std::string_view value) noexcept
    {
        headers_.set(name, value);
        return true;
    }

    bool request::set(const field name, const std::string_view value) noexcept
    {
        const std::string_view key = to_string(name);
        if (key.empty())
        {
            return false;
        }
        headers_.set(key, value);
        return true;
    }

    std::string_view request::at(const std::string_view name) const noexcept
    {
        return headers_.retrieve(name);
    }

    std::string_view request::at(const field name) const noexcept
    {
        const std::string_view key = to_string(name);
        if (key.empty())
        {
            return {};
        }
        return headers_.retrieve(key);
    }

    void request::body(const std::string_view body_value)
    {
        body_.assign(body_value.begin(), body_value.end());
        content_length(static_cast<std::uint64_t>(body_.size()));
    }

    std::string_view request::body() const noexcept
    {
        return body_;
    }

    void request::content_length(const std::uint64_t length)
    {
        char buffer[32]{};
        const auto result = std::to_chars(std::begin(buffer), std::end(buffer), length);
        if (result.ec != std::errc{})
        {
            return;
        }
        const std::string_view value(buffer, static_cast<std::size_t>(result.ptr - buffer));
        headers_.set("Content-Length", value);
    }

    void request::erase(const std::string_view name) noexcept
    {
        static_cast<void>(headers_.erase(name));
    }

    void request::erase(const field name) noexcept
    {
        const std::string_view key = to_string(name);
        if (key.empty())
        {
            return;
        }
        static_cast<void>(headers_.erase(key));
    }

    void request::erase(const std::string_view name, const std::string_view value) noexcept
    {
        static_cast<void>(headers_.erase(name, value));
    }

    void request::erase(const field name, const std::string_view value) noexcept
    {
        const std::string_view key = to_string(name);
        if (key.empty())
        {
            return;
        }
        static_cast<void>(headers_.erase(key, value));
    }

    void request::clear()
    {
        method_ = verb::get;
        method_string_.clear();
        target_.clear();
        body_.clear();
        headers_.clear();
        version_ = 11;
        keep_alive_ = false;
    }

    void request::keep_alive(const bool value) noexcept
    {
        keep_alive_ = value;
        if (value)
        {
            headers_.set("Connection", "keep-alive");
        }
        else
        {
            headers_.set("Connection", "close");
        }
    }

    bool request::empty() const noexcept
    {
        return target_.empty() && headers_.empty() && body_.empty();
    }

    headers &request::header() noexcept
    {
        return headers_;
    }

} // namespace ngx::http
