# 收集库源文件
set(FORWARD_LIB_SOURCES
        forward-engine/http/header.cpp
        forward-engine/http/request.cpp
)

# 创建静态库
add_library(${PROJECT_NAME}_lib STATIC
        ${FORWARD_LIB_SOURCES}
        ../include/forward-engine/core/agent.hpp
        ../include/forward-engine/http/header.hpp
        ../include/forward-engine/http/request.hpp
)

# 设置目标属性
target_include_directories(${PROJECT_NAME}_lib
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ../include/forward-engine/core
        ../include/forward-engine/http
)

# 链接依赖
target_link_libraries(${PROJECT_NAME}_lib
        PUBLIC
        async_simple::async_simple
        Boost::system
)

# 创建可执行程序
add_executable(${PROJECT_NAME}
        main.cpp
)

target_link_libraries(${PROJECT_NAME}
        PRIVATE
        ${PROJECT_NAME}_lib
)
