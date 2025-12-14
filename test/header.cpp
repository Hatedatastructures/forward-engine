#include <header.hpp>
#include <iostream>

int main()
{
    namespace http = ngx::http;
    http::headers headers;
    headers.construct("Content-Type", "text/html");
    headers.construct("Content-Length", "123");
    headers.set("Content-Type", "text/plain");
    std::cout << headers.size() << std::endl;

    for (auto &header : headers)
    {
        std::cout << header.key.value() << " " << header.value << std::endl;
    }
    return 0;
}
