#include <agent/distributor.hpp>

namespace ngx::agent
{
   distributor::distributor(source &pool, net::io_context &ioc, std::pmr::memory_resource *mr)
       : pool_(pool), resolver_(ioc), mr_(mr ? mr : std::pmr::new_delete_resource()), reverse_map_(mr_)
   {
   }

   /**
    * @brief 给 HTTP 正向代理用 (查 DNS)
    * @param host 目标主机名
   * @param port 目标端口号
   * @return 一个指向内部连接对象的智能指针
   */
   net::awaitable<internal_ptr> distributor::route_forward(const std::string_view host, const std::string_view port)
   {
      // 1. 查 DNS
      if (blacklist_.domain(host))
      {
         throw std::runtime_error("Domain blacklisted");
      }
      const auto results = co_await resolver_.async_resolve(host, port, net::use_awaitable);
      // 2. 找池子要连接
      co_return co_await pool_.acquire_tcp(*results.begin());
   }

   /**
    * @brief 给 HTTP 反向代理用 (查静态表)
   * @param host 目标主机名
   * @return 一个指向内部连接对象的智能指针
   */
   net::awaitable<internal_ptr> distributor::route_reverse(const std::string_view host)
   {
      // 1. 查配置表 (比如 configuration.json 加载进来的 map)
      if (auto it = reverse_map_.find(host); it != reverse_map_.end())
      {
         co_return co_await pool_.acquire_tcp(it->second);
      }
      throw std::runtime_error("Unknown host");
   }

   /**
    * @brief 直接连接到指定的 IP 地址
    * @param ep 目标 IP 地址和端口
    * @return 一个指向内部连接对象的智能指针
    */
   net::awaitable<internal_ptr> distributor::route_direct(const tcp::endpoint ep) const
   {
      co_return co_await pool_.acquire_tcp(ep);
   }

}
