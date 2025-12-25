

/**
 * @file connection.cpp
 * @brief 连接缓存实现
 * @details 用于管理 TCP 连接的缓存，支持空闲连接的自动清理。
 * @note v1 shared_ptr 会对cpu缓存不友好，因为会导致缓存的空闲连接被频繁地从内存中移除。
 * 并且连接池全局共享线程越多竞争越激烈，导致性能下降。
 */

// #include <agent/connection.hpp>

// namespace ngx::agent
// {
//     cache::cache(net::io_context &ioc, std::size_t max_connections)
//         : ioc_(ioc), timer_(ioc), strand_(ioc.get_executor()), max_connections_(max_connections)
//     {
//     }

//     /**
//      * @brief 设置连接空闲超时时间
//      * @param timeout 连接空闲超时时间
//      */
//     void cache::idle(std::chrono::seconds timeout)
//     {
//         // 确保在 strand 上修改，避免数据竞争
//         auto set_timeout = [self = shared_from_this(), timeout]()
//         {
//             self->idle_timeout_ = timeout;
//         };
//         net::dispatch(strand_, set_timeout);
//     }

//     /**
//      * @brief 设置连接清理间隔时间
//      * @param interval 连接清理间隔时间
//      */
//     void cache::cleanup(std::chrono::seconds interval)
//     {
//         // 确保在 strand 上修改，避免数据竞争
//         auto set_timeout = [self = shared_from_this(), interval]()
//         {
//             self->cleanup_timeout_ = interval;
//         };
//         net::dispatch(strand_, set_timeout);
//     }

//     /**
//      * @brief 启动后台清理协程
//      */
//     void cache::start()
//     {
//         // 在管理器的 strand 上启动看门狗
//         net::co_spawn(strand_, watchdog(), net::detached);
//     }

//     /**
//      * @brief 关闭连接池
//      * @details 停止看门狗，取消所有等待，关闭所有空闲连接
//      */
//     void cache::shutdown()
//     {
//         auto do_shutdown = [self = shared_from_this()]()
//         {
//             if (self->shutdown_) return;
//             self->shutdown_ = true;

//             // 1. 取消看门狗定时器
//             try { self->timer_.cancel(); } catch(...) {}

//             while (!self->waiters_.empty())
//             {
//                 auto weak_timer = self->waiters_.front();
//                 self->waiters_.pop_front();
//                 if (const auto timer = weak_timer.lock())
//                 {
//                     try { timer->cancel(); } catch(...) {}
//                 }
//             }

//             // 3. 关闭所有缓存的连接
//             for (auto &list: self->cache_ | std::views::values)
//             {
//                 for (const auto&[socket_, last_used] : list)
//                 {
//                     boost::system::error_code ignore;
//                     socket_->close(ignore);
//                 }
//             }
//             self->cache_.clear();
//         };

//         net::dispatch(strand_, do_shutdown);
//     }

//     /**
//      * @brief 通知一个等待中的协程
//      * @note 用于唤醒等待中的协程, 使其继续执行
//      */
//     void cache::notify_one()
//     {
//         if (shutdown_) return;

//         while (!waiters_.empty())
//         {
//             auto weak_timer = waiters_.front();
//             waiters_.pop_front();
//             // 检查定时器是否死亡
//             if (const auto timer = weak_timer.lock())
//             {   // 确保定时器未过期
//                 try { timer->cancel(); } catch(...) {}
//                 return;
//             }
//         }
//     }

//     /**
//      * @brief 检查连接是否有效（僵尸连接检测）
//      * @param socket 要检查的 socket
//      * @return true 有效, false 无效（已关闭）
//      */
//     bool cache::zombie_connection(const std::shared_ptr<tcp::socket>& socket)
//     {
//         if (!socket || !socket->is_open()) return false;

//         boost::system::error_code ec;

//         socket->non_blocking(true, ec);
//         if (ec)
//         {   // 设置非阻塞失败，视为坏连接
//             socket->close(ec);
//             return false;
//         }

//         char buf;
//         // 使用 MSG_PEEK 检查 socket 状态
//         // 因为是非阻塞模式，如果没有数据会返回 would_block
//         const auto n = socket->receive(net::buffer(&buf, 1),
//             tcp::socket::message_peek, ec);

//         if (ec)
//         {
//             if (ec != net::error::would_block && ec != net::error::try_again)
//             {   // 出错且不是 EWOULDBLOCK，说明连接已断开或出错
//                 socket->close(ec);
//                 return false;
//             }
//         }
//         else if (n == 0)
//         {
//             // 收到 EOF (FIN)
//             socket->close(ec);
//             return false;
//         }

//         return true;
//     }

//     /**
//      * @brief 获取 TCP socket (协程调用)
//      * @param endpoint 目标 TCP 端点
//      * @param timeout 获取超时时间
//      * @return `net::awaitable<std::shared_ptr<tcp::socket>>` 已连接的 TCP socket 实例
//      */
//     auto cache::acquire_tcp(const tcp::endpoint endpoint, const std::chrono::seconds timeout)
//         -> net::awaitable<std::shared_ptr<tcp::socket>>
//     {
//         co_await net::dispatch(strand_, net::use_awaitable);

//         if (shutdown_)
//         {
//             throw boost::system::system_error(net::error::operation_aborted);
//         }

//         while (true)
//         {
//             // A. 尝试复用
//             if (auto it = cache_.find(endpoint); it != cache_.end() && !it->second.empty())
//             {
//                 auto [socket_, last_used] = it->second.front();
//                 it->second.pop_front();
//                 if (it->second.empty())
//                     cache_.erase(it);

//                 // 僵尸连接检测
//                 if (zombie_connection(socket_))
//                 {
//                     co_return socket_;
//                 }

//                 // 这是一个坏掉的 socket，让其自动销毁触发 deleter 修正计数
//                 continue;
//             }

//             // B. 检查配额
//             if (active_count_ < max_connections_)
//             {
//                 break;
//             }

//             // LRU 驱逐策略：如果已达上限，尝试强制销毁一个最老的空闲连接
//             if (!cache_.empty())
//             {
//                 // 寻找最老的连接
//                 auto oldest_it = cache_.end();
//                 auto oldest_list_it = std::list<cache::idle_connection>::iterator();
//                 std::chrono::steady_clock::time_point oldest_time = std::chrono::steady_clock::time_point::max();

//                 for (auto map_it = cache_.begin(); map_it != cache_.end(); ++map_it)
//                 {
//                     if (map_it->second.empty()) continue;

//                     // 链表尾部通常是较新的，那么 front 就是最老的
//                     if (auto& list = map_it->second; list.front().last_used < oldest_time)
//                     {
//                         oldest_time = list.front().last_used;
//                         oldest_it = map_it;
//                         oldest_list_it = list.begin();
//                     }
//                 }

//                 if (oldest_it != cache_.end())
//                 {
//                     // 驱逐它
//                     boost::system::error_code ignore;
//                     oldest_list_it->socket->close(ignore); // 关闭 socket
//                     oldest_it->second.erase(oldest_list_it); // 从缓存移除
//                     if (oldest_it->second.empty())
//                     {
//                         cache_.erase(oldest_it);
//                     }

//                 }
//             }

//             // C. 等待可用资源 (带超时)
//             auto timer = std::make_shared<net::steady_timer>(ioc_);
//             timer->expires_after(timeout);
//             waiters_.emplace_back(timer);

//             boost::system::error_code ec;
//             co_await timer->async_wait(net::redirect_error(net::use_awaitable, ec));

//             if (ec != net::error::operation_aborted)
//             {
//                 // 从等待队列中移除自己
//                 throw boost::system::system_error(net::error::timed_out);
//             }

//             // 如果是 operation_aborted，说明被 notify_one 唤醒了（或者 shutdown）
//             if (shutdown_) throw boost::system::system_error(net::error::operation_aborted);
//         }

//         // D. 创建新连接
//         ++active_count_;

//         // 使用自定义 deleter 自动管理计数
//         auto deleter = [weak_self = weak_from_this()](const tcp::socket *s)
//         {
//             if (auto self = weak_self.lock())
//             {
//                 auto renew = [self = self]()
//                 {
//                     if (self->active_count_ > 0)
//                     {
//                         --self->active_count_;
//                         self->notify_one();
//                     }
//                 }; // 在socket 死亡时调用，归还令牌，以便创建新的tcp连接，防止连接爆炸
//                 net::post(self->strand_, renew);
//             }
//             delete s;
//         };

//         auto sock = std::shared_ptr<tcp::socket>(new tcp::socket(ioc_), deleter);

//         boost::system::error_code ec; // 建立连接
//         co_await sock->async_connect(endpoint, net::redirect_error(net::use_awaitable, ec));

//         if (ec)
//         {
//             throw boost::system::system_error(ec);
//         }

//         sock->non_blocking(true, ec); // 非阻塞模式，后续操作均为异步
//         if (ec)
//         {
//             throw boost::system::system_error(ec);
//         }

//         co_return sock;
//     }

//     /**
//      * @brief 释放 TCP socket (协程调用)
//      * @param socket 要释放的 TCP socket 实例
//      * @param endpoint 关联的 TCP 端点
//      */
//     auto cache::release_tcp(const std::shared_ptr<tcp::socket> socket, const tcp::endpoint endpoint)
//         -> net::awaitable<void>
//     {
//         co_await net::dispatch(strand_, net::use_awaitable);

//         if (shutdown_) co_return;

//         if (!socket || !socket->is_open())
//         {
//             // 无效 socket，直接丢弃，deleter 会处理计数
//             co_return;
//         }

//         // 放入池子
//         cache_[endpoint].push_back({socket, std::chrono::steady_clock::now()});

//         // 唤醒等待者（虽然计数没变，但池子有货了）
//         notify_one();
//     }

//     /**
//      * @brief 获取 UDP socket (协程调用)
//      * @param timeout 等待超时时间，默认 5 秒
//      * @return `std::shared_ptr<udp::socket>` 未连接的 UDP socket 实例
//      *
//      */
//     auto cache::acquire_udp(const std::chrono::seconds timeout)
//         -> net::awaitable<std::shared_ptr<udp::socket>>
//     {
//         co_await net::dispatch(strand_, net::use_awaitable);

//         if (shutdown_)
//         {
//             throw boost::system::system_error(net::error::operation_aborted);
//         }

//         while (active_count_ >= max_connections_)
//         {   // 等待直到有空闲连接
//             auto timer = std::make_shared<net::steady_timer>(ioc_);
//             timer->expires_after(timeout);
//             waiters_.emplace_back(timer);

//             boost::system::error_code ec;
//             co_await timer->async_wait(net::redirect_error(net::use_awaitable, ec));

//             if (ec != net::error::operation_aborted)
//             {
//                 throw boost::system::system_error(net::error::timed_out);
//             }

//             if (shutdown_) throw boost::system::system_error(net::error::operation_aborted);
//         }

//         ++active_count_;

//         auto deleter = [weak_self = weak_from_this()](const udp::socket *s)
//         {
//             if (auto self = weak_self.lock())
//             {
//                 auto renew = [self = self]()
//                 {
//                     if (self->active_count_ > 0)
//                     {
//                         --self->active_count_;
//                         self->notify_one();
//                     }
//                 }; // 在socket 死亡时调用，归还令牌，以便创建新的udp连接，防止连接爆炸
//                 net::post(self->strand_, renew);
//             }
//             delete s;
//         };

//         auto sock = std::shared_ptr<udp::socket>(new udp::socket(ioc_), deleter);
//         sock->open(udp::v4());
//         co_return sock;
//     }

//     /**
//      * @brief 释放 UDP 计数
//      */
//     auto cache::release_udp()
//         -> net::awaitable<void>
//     {
//         // UDP socket 由 deleter 管理计数，这里无需操作
//         co_return;
//     }

//     /**
//      * @brief 后台看门狗协程
//      * @note 运行在 strand_ 上，无需加锁保护
//      */
//     auto cache::watchdog()
//         -> net::awaitable<void>
//     {
//         // 使用 weak_ptr 避免循环引用，允许 cache 正常析构
//         const auto weak_self = weak_from_this();

//         while (true)
//         {
//             // 每次循环前检查 cache 是否还活着
//             auto self = weak_self.lock();
//             if (!self || self->shutdown_) co_return;

//             self.reset();

//             self = weak_self.lock();
//             if (!self || self->shutdown_) co_return;

//             self->timer_.expires_after(self->cleanup_timeout_);
//             boost::system::error_code ec;

//             // 用 shutdown() 来做退出机制。
//             // 这样 watchdog 可以放心地持有 self。

//             co_await self->timer_.async_wait(net::redirect_error(net::use_awaitable, ec));

//             if (ec == net::error::operation_aborted || self->shutdown_)
//                 co_return; // 定时器被取消或收到停机信号

//             auto now = std::chrono::steady_clock::now();

//             // 遍历池子清理超时连接
//             for (auto it = self->cache_.begin(); it != self->cache_.end();)
//             {
//                 auto &list = it->second;
//                 for (auto list_it = list.begin(); list_it != list.end();)
//                 {
//                     if (now - list_it->last_used > self->idle_timeout_)
//                     {
//                         boost::system::error_code ignore;
//                         list_it->socket->close(ignore);
//                         list_it = list.erase(list_it); // shared_ptr 销毁 -> deleter 触发 -> 计数减少并唤醒
//                     }
//                     else
//                     {
//                         ++list_it;
//                     }
//                 }

//                 if (list.empty())
//                 {
//                     it = self->cache_.erase(it);
//                 }
//                 else
//                 {
//                     ++it;
//                 }
//             }
//         }
//     }
// }

#include <ranges>
#include <agent/connection.hpp>

namespace ngx::agent
{


    void deleter::operator()(tcp::socket *ptr) const
    {
        if (pool)
        {
            pool->recycle(ptr); // 归还连接
        }
        else
        {
            delete ptr; 
        }
    }


    /**
     * @brief 僵尸检测
     * @details 检查 TCP 连接是否已断开 (对端关闭了连接)。
     * @param s 要检查的 socket 指针
     * @return true 如果连接已断开 (对端关闭了连接)
     * @return false 如果连接正常 (对端还在发送数据)
     */
    bool source::zombie_detection(tcp::socket *s)
    {
        if (!s->is_open())
            return false;

        // 使用 MSG_PEEK | MSG_DONTWAIT 偷看内核缓冲区
        char buf[1];
        boost::system::error_code ec;

        // 使用 native_handle 直接调用系统 recv，或者用 asio 的非阻塞 receive
        s->non_blocking(true, ec);
        if (ec)
            return false;

        auto n = s->receive(boost::asio::buffer(buf, 1),
                            tcp::socket::message_peek, ec);

        s->non_blocking(false, ec); // 恢复阻塞模式 (或者根据你的架构保持非阻塞)

        // 1. 如果读到 0 字节，说明对端关闭了连接 (FIN) -> 僵尸
        if (n == 0 && !ec)
            return false;

        // 2. 如果报错
        if (ec)
        {
            // EAGAIN / EWOULDBLOCK 表示连接正常但没数据 -> 活的
            if (ec == boost::asio::error::would_block || ec == boost::asio::error::try_again)
            {
                return true;
            }
            return false;
        }

        return true;
    }

    /**
     * @brief 获取一个 TCP 连接
     * @details 优先复用缓存中的连接。会自动剔除已断开或超时的僵尸连接。
     * 如果无可用连接，则立即新建并连接。
     */
    net::awaitable<internal_ptr> source::acquire_tcp(tcp::endpoint endpoint)
    {
        // 1. 尝试从缓存获取
        if (const auto it = cache_.find(endpoint); it != cache_.end())
        {
            auto &stack = it->second;

            // 循环直到找到一个健康的连接，或者栈被掏空
            while (!stack.empty())
            {
                auto [socket, last_used] = stack.back();
                stack.pop_back();

                tcp::socket *s = socket;

                // 检查 A: 是否超时
                if (auto now = std::chrono::steady_clock::now(); now - last_used > max_idle_time_)
                {
                    delete s; // 太老了，扔掉
                    continue;
                }

                // 检查 B: 是否僵尸
                if (zombie_detection(s))
                {
                    co_return internal_ptr(s, deleter{this});
                }

                delete s;
            }

            // 如果栈空了，删除 key
            if (stack.empty())
            {
                cache_.erase(it);
            }
        }

        // 2. 缓存没命中（或都是坏的），新建连接
        auto sock = internal_ptr(new tcp::socket(ioc_), deleter{this});

        boost::system::error_code ec;
        co_await sock->async_connect(endpoint, net::redirect_error(net::use_awaitable, ec));

        if (ec)
        {
            throw boost::system::system_error(ec);
        }

        // 3. 设置 socket 选项
        // TCP_NODELAY 对于代理至关重要，减少延迟
        sock->set_option(tcp::no_delay(true));

        co_return sock;
    }

    /**
     * @brief 归还连接（内部接口）
     * @details 由 deleter 析构器自动调用，不要手动调用。
     */
    void source::recycle(tcp::socket *s)
    {
        // 1. 基础健康检查
        if (!s->is_open())
        {
            delete s;
            return;
        }

        // 2. 尝试获取 endpoint 以归类
        try
        {
            // 注意：如果 socket 处于半关闭状态，remote_endpoint 可能会抛出异常
            auto ep = s->remote_endpoint();
            auto &stack = cache_[ep];

            // 3. 资源限制保护
            // 如果这个目标存了太多空闲连接，就别存了，直接销毁，防止占满 FD
            if (stack.size() >= max_cache_endpoint_)
            {
                boost::system::error_code ignore;
                s->close(ignore); // 优雅关闭
                delete s;
                return;
            }
            stack.push_back({s, std::chrono::steady_clock::now()});
        }
        catch (...)
        {
            delete s;
        }
    }

    void source::clear()
    {
        for (auto &stack: cache_ | std::views::values)
        {
            for (const auto &[socket, last_used] : stack)
            {
                if (socket)
                {
                    boost::system::error_code ignore;
                    socket->close(ignore);
                    delete socket;
                }
            }
        }
        cache_.clear();
    }

}