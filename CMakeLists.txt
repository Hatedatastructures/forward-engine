cmake_minimum_required(VERSION 3.15)
project(Forward LANGUAGES CXX)

cmake_policy(SET CMP0167 NEW)


# 全局设置
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 依赖管理
list(APPEND CMAKE_PREFIX_PATH "c:/bin")
find_package(async_simple CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

enable_testing()

# 添加子目录
add_subdirectory(src)
add_subdirectory(test)

