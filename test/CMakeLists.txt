cmake_minimum_required(VERSION 3.15)

# 测试可执行程序
add_executable(headers_test
        header.cpp
)

target_link_libraries(headers_test
        PRIVATE
        ${PROJECT_NAME}_lib
)

add_test(NAME headers_test COMMAND headers_test)

