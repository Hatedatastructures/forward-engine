<<<<
# 项目进度文档

## 1. 项目概况
- **名称**: ForwardEngine
- **目标**: 基于 C++20 和 Boost.Asio 的高性能通用代理引擎
- **核心技术**: C++20 Coroutines, Boost.Asio, Boost.Beast, OpenSSL

## 2. 模块进度

### 2.1 传输层 (Transport Layer)
- [x] **Obscura 组件 (`agent/obscura.hpp`)**
    - [x] 基于 WebSocket 的异步通信封装
    - [x] 支持服务端 (`role::server`) 和客户端 (`role::client`) 模式
    - [x] 解决握手阶段数据预读导致的丢失问题 (使用内部 `buffer_`)
    - [x] 优化 `async_read` 接口，支持外部缓冲区以减少拷贝
    - [x] 修复 `async_accept` 参数匹配错误

### 2.2 构建系统 (Build System)
- [x] CMake 项目结构搭建
- [x] 静态库 (`forward_engine_static_library`) 配置
- [x] 测试工程 (`test`) 配置
- [x] **OpenSSL 依赖配置** (已修复 MinGW 环境下的链接问题)

## 3. 待办事项 (To-Do List)

### 3.1 近期任务
- [x] **修复 OpenSSL 链接错误**: 确保在 Windows/MinGW 环境下正确链接 `libssl` 和 `libcrypto`。
- [ ] **完善测试用例**: 运行 `test/obscura.cpp` 验证双向握手和数据传输。

### 3.2 核心功能开发
- [ ] **内存池 (Memory Pool)**: 优化 `beast::flat_buffer` 和 I/O 操作的内存分配，减少碎片化。
- [ ] **多路复用 (Multiplexing)**:
    - [ ] 设计帧协议 (Frame Protocol)
    - [ ] 实现流 ID (Stream ID) 映射与会话管理
- [ ] **协议支持**:
    - [ ] 探索 QUIC/HTTP3 支持 (基于 UDP 的多路复用)
    - [ ] 完善 TCP/UDP 代理逻辑

## 4. 已知问题 (Known Issues)
- `src/CMakeLists.txt`: 在 Windows MinGW 环境下链接 OpenSSL 时可能会遇到找不到符号的问题，需检查库路径和变量名 (`OPENSSL_LIBRARIES` vs `OpenSSL::SSL`).
====
# 项目进度文档

## 1. 项目概况
- **名称**: forward-engine
- **目标**: 基于 C++20 和 Boost.Asio 的高性能通用代理引擎
- **核心技术**: C++20 Coroutines, Boost.Asio, Boost.Beast, OpenSSL

## 2. 模块进度

### 2.1 传输层 (Transport Layer)
- [x] **Obscura 组件 (`agent/obscura.hpp`)**
    - [x] 基于 WebSocket 的异步通信封装
    - [x] 支持服务端 (`role::server`) 和客户端 (`role::client`) 模式
    - [x] 解决握手阶段数据预读导致的丢失问题 (使用内部 `buffer_`)
    - [x] 优化 `async_read` 接口，支持外部缓冲区以减少拷贝
    - [x] 修复 `async_accept` 参数匹配错误
- [x] **会话管理 (`agent/session.hpp`)**
    - [x] 实现 `socket_concept` 解耦具体协议
    - [x] 引入 `socket_bridge` 适配器统一 TCP/UDP 异步接口
    - [x] 修复变量遮蔽 (Shadowing) 和代码风格问题

### 2.2 连接管理 (Connection Management)
- [x] **连接池 (`agent/connection.hpp`)**
    - [x] 实现 TCP/UDP 连接复用
    - [x] **智能清理**: 实现 LRU (Least Recently Used) 驱逐策略
    - [x] **僵尸检测**: 基于 `MSG_PEEK` 的非阻塞连接有效性检查 (`is_valid_connection`)
    - [x] **优雅退出**: 实现 `shutdown()` 打断看门狗循环引用
    - [x] **分片支持**: 通过 `distributor` 实现连接池分片以减少锁竞争

### 2.3 构建系统 (Build System)
- [x] CMake 项目结构搭建
- [x] 静态库 (`forward_engine_static_library`) 配置
- [x] 测试工程 (`test`) 配置
- [x] **OpenSSL 依赖配置** (已修复 MinGW 环境下的链接问题)
- [x] **文档**: 创建详细的 `README.md`

## 3. 待办事项 (To-Do List)

### 3.1 近期任务
- [x] **修复 OpenSSL 链接错误**: 确保在 Windows/MinGW 环境下正确链接 `libssl` 和 `libcrypto`。
- [x] **连接池测试**: `test/connection_test.cpp` 通过，覆盖复用、超时、LRU 和分片逻辑。
- [ ] **Obscura 测试**: 运行 `test/obscura.cpp` 验证双向握手和数据传输。

### 3.2 核心功能开发
- [ ] **内存池 (Memory Pool)**: 优化 `beast::flat_buffer` 和 I/O 操作的内存分配，减少碎片化。
- [ ] **多路复用 (Multiplexing)**:
    - [ ] 设计帧协议 (Frame Protocol)
    - [ ] 实现流 ID (Stream ID) 映射与会话管理
- [ ] **协议支持**:
    - [ ] 探索 QUIC/HTTP3 支持 (基于 UDP 的多路复用)
    - [ ] 完善 TCP/UDP 代理逻辑 (已完成连接池底层)

## 4. 已知问题 (Known Issues)
- (无主要已知问题，OpenSSL 链接问题已解决)
>>>>