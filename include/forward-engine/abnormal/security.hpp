#pragma once

#include "deviant.hpp"

namespace ngx::abnormal
{
    class security : public exception
    {
    public:
        // 技巧：loc 放在最后一个参数并给默认值，这样抛出时就不用手动传了
        template <typename... Args>
        explicit security(std::format_string<Args...> fmt, Args&&... args,
                                const std::source_location& loc = std::source_location::current())
            : exception(loc, fmt, std::forward<Args>(args)...)
        {}

        // 必须提供非模板的重载，否则某些编译器在只有字符串参数时会歧义
        explicit security(const std::string& msg,
                                const std::source_location& loc = std::source_location::current())
            : exception(loc, msg)
        {}

    protected:
        std::string_view type_name() const noexcept override { return "SECURITY"; }
    };
}