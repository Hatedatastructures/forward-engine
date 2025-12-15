cmake_minimum_required(VERSION 3.15)

# headers 测试可执行程序
add_executable(headers_test
        header.cpp
        request.cpp
        ../include/forward-engine/http/response.hpp
        ../src/forward-engine/http/response.cpp
        ../include/forward-engine/http/serialization.hpp
        ../src/forward-engine/http/serialization.cpp
        ../include/forward-engine/http/deserialization.hpp
        ../src/forward-engine/http/deserialization.cpp
)

target_link_libraries(headers_test
        PRIVATE
        ${PROJECT_NAME}_lib
)

add_test(NAME headers_test COMMAND headers_test)

# 日志模块测试可执行程序
add_executable(log_test
        log.cpp
        ../include/forward-engine/http/serialization.hpp
        ../src/forward-engine/http/serialization.cpp
        ../include/forward-engine/http/deserialization.hpp
        ../src/forward-engine/http/deserialization.cpp
)

target_link_libraries(log_test
        PRIVATE
        ${PROJECT_NAME}_lib
)

add_test(NAME log_test COMMAND log_test)
