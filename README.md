# forward_engine

<div align="center">

![C++20](https://img.shields.io/badge/Standard-C%2B%2B20-blue.svg?logo=c%2B%2B)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey)
![License](https://img.shields.io/badge/License-MIT-green)
![Build](https://img.shields.io/badge/Build-CMake-orange)

</div>

**forward_engine** 是一个基于 C++20 协程（Coroutines）和 Concepts 的高性能网络引擎，旨在提供类似 Nginx 的高并发处理能力，同时保持现代 C++ 的开发体验。它专为构建高吞吐量、低延迟的网络应用而设计。

---

## 🚀 核心特性

- **C++20 原生支持**：全面利用 C++20 协程 (`co_await`)、Concepts (`requires`) 和 Ranges 库，代码简洁且类型安全。
- **高性能异步 I/O**：基于 Boost.Asio 构建，利用各平台的原生异步机制（Windows IOCP, Linux epoll）。
- **智能连接池**：
  - 支持 TCP/UDP 连接复用。
  - 内置 LRU 驱逐策略，自动清理空闲连接。
  - 僵尸连接自动检测与恢复。
  - 分片式锁设计（Sharding），大幅降低多线程竞争。
- **多协议支持**：通过 `obscura` 抽象层，统一支持 TCP, UDP, HTTP, WebSocket 等协议。
- **跨平台**：完美支持 Windows (MinGW/MSVC) 和 Linux 环境。
- **高并发架构**：基于 Strand 的无锁编程模型，确保线程安全的同时最大化性能。

## 🛠️ 技术栈与依赖

本项目依赖以下库（除标准库外）：

- **[Boost](https://www.boost.org/)** (1.75+): 核心网络与系统库 (Asio, Beast, System)。
- **[OpenSSL](https://www.openssl.org/)**: 提供 SSL/TLS 加密支持。
- **CMake** (3.15+): 构建系统。



## 🏗️ 架构概览

- **Agent**: 核心代理层，负责网络 I/O 的调度与分发。
- **Distributor**: 负载均衡器，管理多个连接池分片 (`cache`)，提供高效的资源分配。
- **Connection Cache**: 智能连接池，维护长连接，处理心跳、超时和驱逐。
- **Obscura**: 协议混淆与适配层，允许上层业务逻辑与底层传输协议解耦。

## 📄 许可证

本项目采用 [MIT License](LICENSE) 许可证。