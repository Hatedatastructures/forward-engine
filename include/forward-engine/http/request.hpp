#pragma once
#include <header.hpp>

namespace ngx::http
{
    class request_header
    {
    public:
        request_header() = default;
        request_header(const request_header &other) = default;
        request_header &operator=(const request_header &other) = default;
        ~request_header() = default;
    private:
        headers headers_;
    }; // class request
} // namespace ngx::http
