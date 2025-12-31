#pragma once

#include "deviant.hpp"

namespace ngx::abnormal
{
    class network_error : public exception
    {
    public:
        template <typename... Args>
        explicit network_error(std::format_string<Args...> fmt, Args&&... args,
                               const std::source_location& loc = std::source_location::current())
            : exception(loc, fmt, std::forward<Args>(args)...)
        {}

        explicit network_error(const std::string& msg,
                               const std::source_location& loc = std::source_location::current())
            : exception(loc, msg)
        {}

    protected:
        std::string_view type_name() const noexcept override { return "NETWORK"; }
    };
}