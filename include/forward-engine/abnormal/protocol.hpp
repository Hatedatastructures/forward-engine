#pragma once

#include "deviant.hpp"

namespace ngx::abnormal
{
    class protocol_error : public exception
    {
    public:
        template <typename... Args>
        explicit protocol_error(std::format_string<Args...> fmt, Args&&... args,
                                const std::source_location& loc = std::source_location::current())
            : exception(loc, fmt, std::forward<Args>(args)...)
        {}

        explicit protocol_error(const std::string& msg,
                                const std::source_location& loc = std::source_location::current())
            : exception(loc, msg)
        {}

    protected:
        std::string_view type_name() const noexcept override { return "PROTOCOL"; }
    };
}