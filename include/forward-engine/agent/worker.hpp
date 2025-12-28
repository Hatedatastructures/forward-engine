#pragma once

#include <boost/asio.hpp>
#include <agent/connection.hpp>  // 你已经写好的连接池
#include <agent/distributor.hpp> // 下一步要写的路由器
#include <agent/session.hpp>     // 最后一步要写的会话
#include <memory>

namespace ngx::agent
{

    namespace net = boost::asio;
    using tcp = boost::asio::ip::tcp;

    class worker
    {
    public:
        // 构造函数：初始化所有线程局部资源
        explicit worker(unsigned short port)
            : ioc_(1),                   // 1. 初始化 IO 上下文 (hint=1 表示单线程)
              pool_(ioc_),               // 2. 初始化连接池 (依赖 ioc)
              distributor_(pool_, ioc_), // 3. 初始化路由器 (依赖 pool 和 ioc)
              ssl_ctx_(std::make_shared<net::ssl::context>(net::ssl::context::tlsv12)),
              acceptor_(ioc_) // 4. 初始化接收器
        {
            try
            {
                ssl_ctx_->use_certificate_chain_file("cert.pem");
                ssl_ctx_->use_private_key_file("key.pem", net::ssl::context::pem);
            }
            catch (...)
            {
            }

            auto endpoint = tcp::endpoint(tcp::v4(), port);
            acceptor_.open(endpoint.protocol());
            acceptor_.set_option(net::socket_base::reuse_address(true));

            int one = 1;
#ifdef SO_REUSEPORT
            setsockopt(acceptor_.native_handle(), SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif

            acceptor_.bind(endpoint);
            acceptor_.listen();
        }

        void run()
        {
            do_accept(); // 启动接收
            ioc_.run();  // 启动循环 (卡住线程)
        }

    private:
        void do_accept()
        {
            acceptor_.async_accept(
                [this](boost::system::error_code ec, tcp::socket socket)
                {
                    if (!ec)
                    {
                        // 创建会话，把“路由器”传给它
                        std::make_shared<session<tcp::socket>>(
                            ioc_,
                            std::move(socket),
                            distributor_,
                            ssl_ctx_)
                            ->start();
                    }
                    do_accept();
                });
        }

        net::io_context ioc_;
        source pool_;             // 资源仓库
        distributor distributor_; // 业务大脑
        std::shared_ptr<net::ssl::context> ssl_ctx_;
        tcp::acceptor acceptor_;
    };

}
