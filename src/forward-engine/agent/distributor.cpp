#include <agent/distributor.hpp>
#include <functional>

namespace ngx::agent
{
    distributor::distributor(net::io_context& ioc)
        : ioc_(ioc)
    {
        // 初始化缓存分片
        caches_.reserve(shard_count);
        for (size_t i = 0; i < shard_count; ++i)
        {
            auto c = std::make_shared<cache>(ioc_, 100); // 每个分片 100 连接，总共 400
            c->start();
            caches_.push_back(c);
        }
    }

    void distributor::shutdown()
    {
        for (auto& c : caches_)
        {
            c->shutdown();
        }
        maps.clear();
    }

    std::shared_ptr<cache> distributor::get_cache(const tcp::endpoint& endpoint)
    {
        // 简单的 Hash 取模分片
        // 使用 endpoint 的 IP 和端口作为 hash 键
        std::size_t seed = 0;
        std::hash<std::string> hasher;
        seed ^= hasher(endpoint.address().to_string()) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= std::hash<unsigned short>{}(endpoint.port()) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        
        return caches_[seed % shard_count];
    }

    std::shared_ptr<cache> distributor::get_udp_cache()
    {
        // UDP 轮询分发
        size_t idx = udp_counter_.fetch_add(1, std::memory_order_relaxed);
        return caches_[idx % shard_count];
    }
}
