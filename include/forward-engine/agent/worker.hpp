#pragma once


#include <agent/connection.hpp>
#include <agent/distributor.hpp>


namespace ngx::agent
{
    class worker
    {
    public:
        worker()
            : io_(),
              source_(io_),
              distributor_(io_)
        {
        }

    private:
        net::io_context io_;
        source source_;
        distributor distributor_;
    }; // class worker
}
