#include <header.hpp>
#include <iostream>
#include <boost/beast/http.hpp>

int main()
{
    namespace bhttp = boost::beast::http;
    bhttp::request<bhttp::string_body> req;
    req.version(11);
    req.method(bhttp::verb::get);
    req.target("/");
    req.set(bhttp::field::host, "example.com");
    req.set(bhttp::field::user_agent, "ForwardEngine/1.0");
    req.set(bhttp::field::accept, "*/*");
    req.insert(bhttp::field::content_length, "123");
    req.keep_alive(true);
    // namespace http = ngx::http;
    // http::headers headers;
    // headers.construct("Content-Type", "text/html");
    // headers.construct("Content-Length", "123");
    // headers.set("Content-Type", "text/plain");
    // std::cout << headers.size() << std::endl;

    // for (auto &header : headers)
    // {
    //     std::cout << header.key.value() << " " << header.value << std::endl;
    // }
    // return 0;
}
