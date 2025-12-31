#pragma once
#pragma once

#include <stdexcept>
#include <exception>
#include <string>
#include <string_view>
#include <source_location>
#include <format>
#include <filesystem>

namespace ngx::abnormal
{
    /**
     * @brief 所有自定义异常的基类
     * @note 自动捕获抛出点的位置信息，并支持格式化消息
     */
    class exception : public std::runtime_error
    {
    public:
        template <typename... Args>
        explicit exception(const std::source_location& loc, std::format_string<Args...> fmt, Args&&... args)
            : std::runtime_error(std::format(fmt, std::forward<Args>(args)...))
            , location_(loc)
        {
        }

        explicit exception(const std::source_location& loc, const std::string& msg)
            : std::runtime_error(msg)
            , location_(loc)
        {
        }

        /**
         * @brief 获取异常抛出时的位置信息
         * @return `std::source_location` 包含文件名、行号、列号等位置信息
         */
        [[nodiscard]] const std::source_location& location() const noexcept { return location_; }

        /**
         * @brief 获取异常抛出时的文件名
         * @return `std::string` 文件名（不包含路径）
         */
        [[nodiscard]] std::string filename() const
        {
            return std::filesystem::path(location_.file_name()).filename().string();
        }

        /**
         * @brief 格式化异常信息
         * @return `std::string` 包含文件名、行号、异常类型和错误描述的字符串
         */
        [[nodiscard]] virtual std::string dump() const
        {
            return std::format("[{}:{}] [{}] {}",filename(),location_.line(),
                type_name(),what());
        }

    protected:
        /**
         * @brief 子类必须实现这个方法，返回自己的类型名称（如 "SECURITY", "NETWORK"）
         * @return `std::string_view` 异常类型名称
         */
        [[nodiscard]] virtual std::string_view type_name() const noexcept = 0;

    private:
        std::source_location location_;
    };
}