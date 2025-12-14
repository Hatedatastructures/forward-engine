#pragma once

#include <string>
#include <string_view>
#include <cstdint>
#include <constants.hpp>
#include <header.hpp>

namespace ngx::http
{
    class request
    {
    public:
        request() = default;
        request(const request &other) = default;
        request &operator=(const request &other) = default;
        ~request() = default;

        void method(verb method);
        [[nodiscard]] verb method() const noexcept;
        void method(std::string_view method);
        [[nodiscard]] std::string_view method_string() const noexcept;

        void target(std::string_view target);
        [[nodiscard]] const std::string &target() const noexcept;

        void version(unsigned int value);
        [[nodiscard]] unsigned int version() const noexcept;


        bool set(std::string_view name, std::string_view value) noexcept;
        bool set(field name, std::string_view value) noexcept;

        [[nodiscard]] std::string_view at(std::string_view name) const noexcept;
        [[nodiscard]] std::string_view at(field name) const noexcept;

        void body(std::string_view body);
        [[nodiscard]] std::string_view body() const noexcept;

        void content_length(std::uint64_t length);

        void erase(std::string_view name) noexcept;
        void erase(field name) noexcept;
        void erase(std::string_view name, std::string_view value) noexcept;
        void erase(field name, std::string_view value) noexcept;

        void clear();
        void keep_alive(bool value) noexcept;
        [[nodiscard]] bool empty() const noexcept;
        [[nodiscard]] headers &header() noexcept;

    private:
        verb method_{verb::get};
        std::string method_string_;
        std::string target_;
        std::string body_;
        headers headers_;
        unsigned int version_{11};
        bool keep_alive_{false};
    };
} // namespace ngx::http
