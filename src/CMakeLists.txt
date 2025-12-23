# 收集库源文件
set(FORWARD_LIB_SOURCES
        forward-engine/http/header.cpp
        ../include/forward-engine/http/header.hpp
        forward-engine/http/request.cpp
        ../include/forward-engine/http/request.hpp
        forward-engine/log/monitor.cpp
        ../include/forward-engine/log/monitor.hpp
        forward-engine/http/response.cpp
        ../include/forward-engine/http/response.hpp
        forward-engine/http/serialization.cpp
        ../include/forward-engine/http/serialization.hpp
        forward-engine/http/deserialization.cpp
        ../include/forward-engine/http/deserialization.hpp
        forward-engine/agent/positive.cpp
        ../include/forward-engine/agent/positive.hpp
        forward-engine/agent/reverse.cpp
        ../include/forward-engine/agent/reverse.hpp
        forward-engine/agent/frame.cpp
        ../include/forward-engine/agent/frame.hpp
        forward-engine/agent/distributor.cpp
        ../include/forward-engine/agent/distributor.hpp
        forward-engine/agent/connection.cpp
        ../include/forward-engine/agent/connection.hpp
        ../include/forward-engine/agent/obscura.hpp
        ../include/forward-engine/agent/session.hpp
        ../include/forward-engine/limit/blacklist.hpp
        ../include/forward-engine/http.hpp
        ../include/forward-engine/http/constants.hpp
)

# 创建静态库
add_library(${PROJECT_NAME}_static_library STATIC
        ${FORWARD_LIB_SOURCES}
)

# 设置目标属性
target_include_directories(${PROJECT_NAME}_static_library
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include/forward-engine
        ${boost_include_dir}
        ${Boost_INCLUDE_DIRS}
)

# 链接依赖
target_link_libraries(${PROJECT_NAME}_static_library
        PUBLIC
        async_simple::async_simple
        Boost::system
        OpenSSL::SSL
        OpenSSL::Crypto
        ws2_32
        mswsock
        crypt32
)

target_compile_definitions(${PROJECT_NAME}_static_library
        PUBLIC
        BOOST_ASIO_HEADER_ONLY
)

# 创建可执行程序
add_executable(${PROJECT_NAME}
        main.cpp
)

target_link_libraries(${PROJECT_NAME}
        PRIVATE
        ${PROJECT_NAME}_static_library
)
