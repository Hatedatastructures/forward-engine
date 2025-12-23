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
     * @brief 通知一个等待中的协程
     * @note 用于唤醒等待中的协程, 使其继续执行
     */
    void cache::notify_one()
    {
        while (!waiters_.empty())
        {
            auto weak_timer = waiters_.front();
            waiters_.pop_front();
            // 检查定时器是否死亡
            if (auto timer = weak_timer.lock())
            {   // 确保定时器未过期
                timer->cancel();
                return;
            }
        }
    }

    /**
     * @brief 获取 TCP socket (协程调用)
     * @param endpoint 目标 TCP 端点
     * @return `net::awaitable<std::shared_ptr<tcp::socket>>` 已连接的 TCP socket 实例
     */
    net::awaitable<std::shared_ptr<tcp::socket>> cache::acquire_tcp(tcp::endpoint endpoint)
    {
        co_await net::dispatch(strand_, net::use_awaitable);

        while (true)
        {
            // A. 尝试复用
            auto it = cache_.find(endpoint);
            if (it != cache_.end() && !it->second.empty())
            {
                auto conn = it->second.front();
                it->second.pop_front();
                if (it->second.empty())
                    cache_.erase(it);

                if (conn.socket_->is_open())
                {
                    // 找到了复用的，直接返回
                    co_return conn.socket_;
                }

                // 这是一个坏掉的 socket，让其自动销毁触发 deleter 修正计数
                continue;
            }

            // B. 检查配额, 如果超过最大连接数, 则等待, 否则跳出循环直接创建新连接
            if (active_count_ < max_connections_)
            {
                break;
            }

            // C. 等待可用资源
            auto timer = std::make_shared<net::steady_timer>(ioc_);
            timer->expires_at(std::chrono::steady_clock::time_point::max());
            waiters_.emplace_back(timer); // 放入等待队列，等待其他socket销毁调用notify_one函数归还令牌

            boost::system::error_code ec;
            co_await timer->async_wait(net::redirect_error(net::use_awaitable, ec));
        }

        // D. 创建新连接
        ++active_count_;
        
        // 使用自定义 deleter 自动管理计数
        auto deleter = [weak_self = weak_from_this()](tcp::socket *s) 
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
    net::awaitable<void> cache::release_tcp(std::shared_ptr<tcp::socket> socket, tcp::endpoint endpoint)
    {
        co_await net::dispatch(strand_, net::use_awaitable);

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
    net::awaitable<std::shared_ptr<udp::socket>> cache::acquire_udp()
    {
        co_await net::dispatch(strand_, net::use_awaitable);

        while (active_count_ >= max_connections_)
        {
            auto timer = std::make_shared<net::steady_timer>(ioc_);
            timer->expires_at(std::chrono::steady_clock::time_point::max());
            waiters_.emplace_back(timer);

            boost::system::error_code ec;
            co_await timer->async_wait(net::redirect_error(net::use_awaitable, ec));
        }

        ++active_count_;

        auto deleter = [weak_self = weak_from_this()](udp::socket *s) 
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
    net::awaitable<void> cache::release_udp()
    {
        // UDP socket 由 deleter 管理计数，这里无需操作
        co_return;
    }

    /**
     * @brief 后台看门狗协程
     * @note 运行在 strand_ 上，无需加锁保护
     */
    net::awaitable<void> cache::watchdog()
    {
        // 保活 cache 实例，防止在协程运行期间被销毁
        // 注意：这会创建一个循环引用，直到 io_context 停止或显式打破
        auto self = shared_from_this();

        // 初始化
        timer_.expires_after(cleanup_timeout_);

        while (true)
        {
            boost::system::error_code ec;
            co_await timer_.async_wait(net::redirect_error(net::use_awaitable, ec));

            if (ec == net::error::operation_aborted)
                co_return; // 定时器被取消

            auto now = std::chrono::steady_clock::now();


            for (auto it = cache_.begin(); it != cache_.end();)
            {
                auto &list = it->second;
                for (auto list_it = list.begin(); list_it != list.end();)
                {   // list 结构遍历
                    if (now - list_it->last_used > idle_timeout_)
                    {
                        boost::system::error_code ignore;
                        list_it->socket_->close(ignore);
                        list_it = list.erase(list_it);
                        // shared_ptr 销毁 -> deleter 触发 -> 连接计数减少并唤醒任务 -> acquire_tcp 获取令牌创建连接
                    }
                    else
                    {
                        ++list_it;
                    }
                }

                if (list.empty())
                {
                    it = cache_.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            // 重置
            timer_.expires_after(cleanup_timeout_);
        }
    }
}
