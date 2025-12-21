cmake_minimum_required(VERSION 3.15)

# headers 测试可执行程序
add_executable(headers_test
        header.cpp
)

target_link_libraries(headers_test
        PRIVATE
        ${PROJECT_NAME}_static_library
)

add_test(NAME headers_test COMMAND headers_test)

# request 测试可执行程序
add_executable(request_test
        request.cpp
)

target_link_libraries(request_test
        PRIVATE
        ${PROJECT_NAME}_static_library
)

add_test(NAME request_test COMMAND request_test)

# 日志模块测试可执行程序
add_executable(log_test
        log.cpp
)

target_link_libraries(log_test
        PRIVATE
        ${PROJECT_NAME}_static_library
)

add_test(NAME log_test COMMAND log_test)

# session 测试可执行程序
add_executable(session_test
        session.cpp
)

target_link_libraries(session_test
        PRIVATE
        ${PROJECT_NAME}_static_library
        Boost::system
        ws2_32
        mswsock
        crypt32
)

add_test(NAME session_test COMMAND session_test)

# obscura 测试可执行程序
add_executable(obscura_test
        obscura.cpp
)

target_link_libraries(obscura_test
        PRIVATE
        ${PROJECT_NAME}_static_library
        async_simple::async_simple
        Boost::system
        ${OPENSSL_LIBRARIES}
        ws2_32
        mswsock
        crypt32
)

add_test(NAME obscura_test COMMAND obscura_test)
