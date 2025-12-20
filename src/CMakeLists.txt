# 查找 OpenSSL
find_package(OpenSSL REQUIRED)
message(STATUS "OpenSSL Include: ${OPENSSL_INCLUDE_DIR}")
message(STATUS "OpenSSL Libs: ${OPENSSL_LIBRARIES}")

# 收集库源文件
set(FORWARD_LIB_SOURCES
        forward-engine/http/header.cpp
        forward-engine/http/request.cpp
        forward-engine/log/monitor.cpp
        ../include/forward-engine/http/response.hpp
        forward-engine/http/response.cpp
        ../include/forward-engine/http/serialization.hpp
        forward-engine/http/serialization.cpp
        ../include/forward-engine/http/deserialization.hpp
        forward-engine/http/deserialization.cpp
        ../include/forward-engine/agent/positive.hpp
        ../include/forward-engine/agent/reverse.hpp
        ../include/forward-engine/http.hpp
        forward-engine/agent/positive.cpp
        forward-engine/agent/reverse.cpp
        ../include/forward-engine/agent/obscura.hpp
        ../include/forward-engine/agent/frame.hpp
        forward-engine/agent/frame.cpp
        ../include/forward-engine/limit/blacklist.hpp
)

# 创建静态库
add_library(${PROJECT_NAME}_static_library STATIC
        ${FORWARD_LIB_SOURCES}
        ../include/forward-engine/http/header.hpp
        ../include/forward-engine/http/request.hpp
        ../include/forward-engine/http/constants.hpp
        ../include/forward-engine/log/monitor.hpp
        ../include/forward-engine/http/response.hpp
        forward-engine/http/response.cpp
        ../include/forward-engine/http/serialization.hpp
        forward-engine/http/serialization.cpp
        ../include/forward-engine/http/deserialization.hpp
        forward-engine/http/deserialization.cpp
        ../include/forward-engine/agent/positive.hpp
        ../include/forward-engine/agent/reverse.hpp
        ../include/forward-engine/http.hpp
        forward-engine/agent/positive.cpp
        forward-engine/agent/reverse.cpp
        ../include/forward-engine/agent/obscura.hpp
        ../include/forward-engine/agent/frame.hpp
        forward-engine/agent/frame.cpp
        ../include/forward-engine/limit/blacklist.hpp
)

# 设置目标属性
target_include_directories(${PROJECT_NAME}_static_library
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ../include/forward-engine
)

# 链接依赖
target_link_libraries(${PROJECT_NAME}_static_library
        PUBLIC
        async_simple::async_simple
        Boost::system
        ${OPENSSL_LIBRARIES}
        ws2_32
        mswsock
        crypt32
)
target_include_directories(${PROJECT_NAME}_static_library PUBLIC ${OPENSSL_INCLUDE_DIR})

# 创建可执行程序
add_executable(${PROJECT_NAME}
        main.cpp
        ../include/forward-engine/http/response.hpp
        forward-engine/http/response.cpp
        ../include/forward-engine/http/serialization.hpp
        forward-engine/http/serialization.cpp
        ../include/forward-engine/http/deserialization.hpp
        forward-engine/http/deserialization.cpp
        ../include/forward-engine/agent/positive.hpp
        ../include/forward-engine/agent/reverse.hpp
        ../include/forward-engine/http.hpp
        forward-engine/agent/positive.cpp
        forward-engine/agent/reverse.cpp
        ../include/forward-engine/agent/obscura.hpp
        ../include/forward-engine/agent/frame.hpp
        forward-engine/agent/frame.cpp
        ../include/forward-engine/limit/blacklist.hpp
)

target_link_libraries(${PROJECT_NAME}
        PRIVATE
        ${PROJECT_NAME}_static_library
)
