#include <request.hpp>

void handle()
{
    using namespace ngx::http;

    request req;

    // 设置请求行
    req.method(verb::post);
    req.target("/api/v1/user");
    req.version(11);

    // 设置头部：字符串接口
    req.set("Host", "example.com");
    req.set("User-Agent", "ForwardEngine/0.1");

    // 设置头部：field 枚举接口
    req.set(field::content_type, "application/json");

    // 设置 body，同时自动填充 Content-Length
    req.body(R"({"name":"test","age":18})");

    // 设置 keep-alive
    req.keep_alive(true);

    // 读取头部
    auto host = req.at(field::host);
    auto ua   = req.at("User-Agent");

    // 删除特定头部
    req.erase(field::user_agent);
}