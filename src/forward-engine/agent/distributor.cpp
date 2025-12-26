#include <agent/distributor.hpp>
#include <functional>

namespace ngx::agent
{
    distributor::distributor(net::io_context& ioc)
        : ioc_(ioc)
    {
    }

    void distributor::shutdown()
    {
        maps_.clear();
    }
}
