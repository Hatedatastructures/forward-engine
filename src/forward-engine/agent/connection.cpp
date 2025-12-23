#include <ranges>
#include <agent/connection.hpp>

namespace ngx::agent
{
    cache::cache(net::io_context &ioc, std::size_t max_connections)
        : ioc_(ioc), timer_(ioc), strand_(ioc.get_executor()), max_connections_(max_connections)
    {
    }

    /**
     * @brief 设置连接空闲超时时间
     * @param timeout 连接空闲超时时间
     */
    void cache::idle(std::chrono::seconds timeout)
    {
        // 确保在 strand 上修改，避免数据竞争
        auto set_timeout = [self = shared_from_this(), timeout]() 
        {
            self->idle_timeout_ = timeout;
        };
        net::dispatch(strand_, set_timeout);
    }

    /**
     * @brief 设置连接清理间隔时间
     * @param interval 连接清理间隔时间
     */
    void cache::cleanup(std::chrono::seconds interval)
    {
        // 确保在 strand 上修改，避免数据竞争
        auto set_timeout = [self = shared_from_this(), interval]() 
        {
            self->cleanup_timeout_ = interval;
        };
        net::dispatch(strand_, set_timeout);
    }

    /**
     * @brief 启动后台清理协程
     */
    void cache::start()
    {
        // 在管理器的 strand 上启动看门狗
        net::co_spawn(strand_, watchdog(), net::detached);
    }

    /**
     * @brief 关闭连接池
     * @details 停止看门狗，取消所有等待，关闭所有空闲连接
     */
    void cache::shutdown()
    {
        auto do_shutdown = [self = shared_from_this()]()
        {
            if (self->shutdown_) return;
            self->shutdown_ = true;

            // 1. 取消看门狗定时器
            try { self->timer_.cancel(); } catch(...) {}

            while (!self->waiters_.empty())
            {
                auto weak_timer = self->waiters_.front();
                self->waiters_.pop_front();
                if (const auto timer = weak_timer.lock())
                {
                    try { timer->cancel(); } catch(...) {}
                }
            }

            // 3. 关闭所有缓存的连接
            for (auto &list: self->cache_ | std::views::values)
            {
                for (const auto&[socket_, last_used] : list)
                {
                    boost::system::error_code ignore;
                    socket_->close(ignore);
                }
            }
            self->cache_.clear();
        };
        
        net::dispatch(strand_, do_shutdown);
    }

    /**
     * @brief 通知一个等待中的协程
     * @note 用于唤醒等待中的协程, 使其继续执行
     */
    void cache::notify_one()
    {
        if (shutdown_) return;

        while (!waiters_.empty())
        {
            auto weak_timer = waiters_.front();
            waiters_.pop_front();
            // 检查定时器是否死亡
            if (const auto timer = weak_timer.lock())
            {   // 确保定时器未过期
                try { timer->cancel(); } catch(...) {}
                return;
            }
        }
    }

    /**
     * @brief 获取 TCP socket (协程调用)
     * @param endpoint 目标 TCP 端点
     * @param timeout 获取超时时间
     * @return `net::awaitable<std::shared_ptr<tcp::socket>>` 已连接的 TCP socket 实例
     */
    auto cache::acquire_tcp(const tcp::endpoint endpoint, const std::chrono::seconds timeout)
        -> net::awaitable<std::shared_ptr<tcp::socket>>
    {
        co_await net::dispatch(strand_, net::use_awaitable);

        if (shutdown_)
        {
            throw boost::system::system_error(net::error::operation_aborted);
        }

        while (true)
        {
            // A. 尝试复用
            if (auto it = cache_.find(endpoint); it != cache_.end() && !it->second.empty())
            {
                auto [socket_, last_used] = it->second.front();
                it->second.pop_front();
                if (it->second.empty())
                    cache_.erase(it);

                // 僵尸连接检测 
                if (socket_->is_open())
                {
                    boost::system::error_code ec;
                    char buf;
                    // 使用 MSG_PEEK | MSG_DONTWAIT 检查 socket 状态
                    // 如果 socket 已断开（收到 FIN），recv 会返回 0 或错误
                    const auto n = socket_->receive(net::buffer(&buf, 1),
                        tcp::socket::message_peek, ec);
                    
                    bool is_stale = false;
                    if (ec && ec != net::error::would_block && ec != net::error::try_again)
                    {
                        is_stale = true;
                    }
                    else if (n == 0)
                    {
                        // 收到 EOF (FIN)
                        is_stale = true;
                    }
                    
                    if (!is_stale)
                    {
                        co_return socket_;
                    }
                    
                    // 是僵尸连接，自动销毁 
                    socket_->close(ec);
                    continue; 
                }

                // 这是一个坏掉的 socket，让其自动销毁触发 deleter 修正计数
                continue;
            }

            // B. 检查配额
            if (active_count_ < max_connections_)
            {
                break;
            }

            // LRU 驱逐策略：如果已达上限，尝试强制销毁一个最老的空闲连接
            if (!cache_.empty())
            {
                // 寻找最老的连接
                auto oldest_it = cache_.end();
                auto oldest_list_it = std::list<cache::idle_connection>::iterator();
                std::chrono::steady_clock::time_point oldest_time = std::chrono::steady_clock::time_point::max();

                for (auto map_it = cache_.begin(); map_it != cache_.end(); ++map_it)
                {
                    if (map_it->second.empty()) continue;
                    
                    // 链表尾部通常是较新的，那么 front 就是最老的
                    if (auto& list = map_it->second; list.front().last_used < oldest_time)
                    {
                        oldest_time = list.front().last_used;
                        oldest_it = map_it;
                        oldest_list_it = list.begin();
                    }
                }

                if (oldest_it != cache_.end())
                {
                    // 驱逐它
                    boost::system::error_code ignore;
                    oldest_list_it->socket_->close(ignore); // 关闭 socket
                    oldest_it->second.erase(oldest_list_it); // 从缓存移除
                    if (oldest_it->second.empty())
                    {
                        cache_.erase(oldest_it);
                    }

                }
            }

            // C. 等待可用资源 (带超时)
            auto timer = std::make_shared<net::steady_timer>(ioc_);
            timer->expires_after(timeout);
            waiters_.emplace_back(timer); 

            boost::system::error_code ec;
            co_await timer->async_wait(net::redirect_error(net::use_awaitable, ec));
            
            if (ec != net::error::operation_aborted)
            {
                // 从等待队列中移除自己
                throw boost::system::system_error(net::error::timed_out);
            }
            
            // 如果是 operation_aborted，说明被 notify_one 唤醒了（或者 shutdown）
            if (shutdown_) throw boost::system::system_error(net::error::operation_aborted);
        }

        // D. 创建新连接
        ++active_count_;
        
        // 使用自定义 deleter 自动管理计数
        auto deleter = [weak_self = weak_from_this()](const tcp::socket *s)
        {
            if (auto self = weak_self.lock())
            {
                auto renew = [self = self]()
                {
                    if (self->active_count_ > 0)
                    {
                        --self->active_count_;
                        self->notify_one();
                    }
                }; // 在socket 死亡时调用，归还令牌，以便创建新的tcp连接，防止连接爆炸
                net::post(self->strand_, renew);
            }
            delete s;
        };

        co_return std::shared_ptr<tcp::socket>(new tcp::socket(ioc_), deleter);
    }

    /**
     * @brief 释放 TCP socket (协程调用)
     * @param socket 要释放的 TCP socket 实例
     * @param endpoint 关联的 TCP 端点
     */
    auto cache::release_tcp(const std::shared_ptr<tcp::socket> socket, const tcp::endpoint endpoint)
        -> net::awaitable<void>
    {
        co_await net::dispatch(strand_, net::use_awaitable);
        
        if (shutdown_) co_return;

        if (!socket || !socket->is_open())
        {
            // 无效 socket，直接丢弃，deleter 会处理计数
            co_return;
        }

        // 放入池子
        cache_[endpoint].push_back({socket, std::chrono::steady_clock::now()});

        // 唤醒等待者（虽然计数没变，但池子有货了）
        notify_one();
    }

    /**
     * @brief 获取 UDP socket (协程调用)
     * @note UDP 无状态，通常不复用，但受全局数量限制
     */
    auto cache::acquire_udp(const std::chrono::seconds timeout)
        -> net::awaitable<std::shared_ptr<udp::socket>>
    {
        co_await net::dispatch(strand_, net::use_awaitable);
        
        if (shutdown_)
        {
            throw boost::system::system_error(net::error::operation_aborted);
        }

        while (active_count_ >= max_connections_) 
        {   // 等待直到有空闲连接
            auto timer = std::make_shared<net::steady_timer>(ioc_);
            timer->expires_after(timeout);
            waiters_.emplace_back(timer);

            boost::system::error_code ec;
            co_await timer->async_wait(net::redirect_error(net::use_awaitable, ec));
            
            if (ec != net::error::operation_aborted)
            {
                throw boost::system::system_error(net::error::timed_out);
            }
            
            if (shutdown_) throw boost::system::system_error(net::error::operation_aborted);
        }

        ++active_count_;

        auto deleter = [weak_self = weak_from_this()](const udp::socket *s)
        {
            if (auto self = weak_self.lock())
            {
                auto renew = [self = self]()
                {
                    if (self->active_count_ > 0)
                    {
                        --self->active_count_;
                        self->notify_one();
                    }
                }; // 在socket 死亡时调用，归还令牌，以便创建新的udp连接，防止连接爆炸
                net::post(self->strand_, renew);
            }
            delete s;
        };

        auto sock = std::shared_ptr<udp::socket>(new udp::socket(ioc_), deleter);
        sock->open(udp::v4());
        co_return sock;
    }

    /**
     * @brief 释放 UDP 计数
     */
    auto cache::release_udp()
        -> net::awaitable<void>
    {
        // UDP socket 由 deleter 管理计数，这里无需操作
        co_return;
    }

    /**
     * @brief 后台看门狗协程
     * @note 运行在 strand_ 上，无需加锁保护
     */
    auto cache::watchdog()
        -> net::awaitable<void>
    {
        // 使用 weak_ptr 避免循环引用，允许 cache 正常析构
        const auto weak_self = weak_from_this();

        while (true)
        {
            // 每次循环前检查 cache 是否还活着
            auto self = weak_self.lock();
            if (!self || self->shutdown_) co_return;
            
            self.reset(); 
            
            self = weak_self.lock();
            if (!self || self->shutdown_) co_return;
            
            self->timer_.expires_after(self->cleanup_timeout_);
            boost::system::error_code ec;
            
            // 用 shutdown() 来做退出机制。
            // 这样 watchdog 可以放心地持有 self。
            
            co_await self->timer_.async_wait(net::redirect_error(net::use_awaitable, ec));

            if (ec == net::error::operation_aborted || self->shutdown_)
                co_return; // 定时器被取消或收到停机信号

            auto now = std::chrono::steady_clock::now();


            // 遍历池子清理超时连接
            for (auto it = self->cache_.begin(); it != self->cache_.end();)
            {
                auto &list = it->second;
                for (auto list_it = list.begin(); list_it != list.end();)
                {
                    if (now - list_it->last_used > self->idle_timeout_)
                    {
                        boost::system::error_code ignore;
                        list_it->socket_->close(ignore);
                        list_it = list.erase(list_it); // shared_ptr 销毁 -> deleter 触发 -> 计数减少并唤醒
                    }
                    else
                    {
                        ++list_it;
                    }
                }

                if (list.empty())
                {
                    it = self->cache_.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
    }
}
