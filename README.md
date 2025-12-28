# forward_engine

<div align="center">

![C++20](https://img.shields.io/badge/Standard-C%2B%2B20-blue.svg?logo=c%2B%2B)
![Platform](https://img.shields.io/badge/Platform-Windows%2011-lightgrey)
![License](https://img.shields.io/badge/License-MIT-green)
![Build](https://img.shields.io/badge/Build-CMake-orange)

</div>

**ForwardEngine** 是一个基于 C++20 协程与 Boost.Asio 的代理引擎原型工程，当前重点在于把“接入（accept）→ 协议识别 → 路由 → 上游连接 → 双向转发”的主链路跑通，并提供可演进的模块边界。

---

## 🚀 核心特性

- **C++20 支持**：使用协程（`co_await`）组织异步流程，减少回调嵌套。
- **异步 I/O**：基于 Boost.Asio/Beast（Windows 11 + MinGW 为主要构建环境）。
- **HTTP 请求处理**：提供基础的 HTTP `request/response/header` 模型与序列化/反序列化。
- **代理会话**：支持 HTTP 正向/反向代理的基本转发链路；支持 CONNECT 隧道转发。
- **连接复用（当前仅 TCP）**：按目标端点缓存空闲连接，并带基础健康检查（僵尸检测/空闲超时/单端点上限）。
- **Obscura 封装**：基于 Beast WebSocket（含 SSL）的传输包装，提供 `handshake/async_read/async_write` 等接口。

## 🛠️ 技术栈与依赖

本项目依赖以下库（除标准库外）：

- **[Boost](https://www.boost.org/)** (1.75+): 核心网络与系统库 (Asio, Beast, System)。
- **[OpenSSL](https://www.openssl.org/)**: 提供 SSL/TLS 加密支持。
- **CMake** (3.15+): 构建系统。

依赖默认从 `c:/bin` 查找（见根 `CMakeLists.txt` 中的 `CMAKE_PREFIX_PATH` 与 `OPENSSL_ROOT_DIR` 配置）。

## 🧱 当前模块边界
- **Agent**：接入与会话调度（`worker/session/analysis`）。
- **Distributor**：路由与连接获取（`route_forward/route_reverse/route_direct`）。
- **Connection Pool**：TCP 连接的获取与回收（`source/internal_ptr/deleter`）。
- **HTTP**：HTTP 类型与编解码（`include/forward-engine/http/*`）。
- **Obscura**：WebSocket(SSL) 传输封装（`agent/obscura.hpp`）。
- **Log**：协程日志输出（`log/monitor.hpp`）。

## ✅ 当前已知限制
- 连接池目前只覆盖 TCP；尚未实现 UDP 缓存、全局 LRU、后台定时清理、跨线程共享/分片池。
- 反向代理路由表 `reverse_map_` 目前未接入配置加载。
- 测试用例中 `connection_test`、`obscura_test` 仍处于未稳定状态（详见 `docs/progress.md`）。

## 📄 许可证

本项目采用 [MIT License](LICENSE) 许可证。
