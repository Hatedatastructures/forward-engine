## 前置知识：读懂 ForwardEngine 需要知道什么

这份文档的目的：帮助第一次打开仓库的人，快速理解这个项目在做什么、关键概念是什么、主流程怎么走、为什么需要这些模块。

### 1. 这个项目解决什么问题

ForwardEngine 是一个基于 C++20 协程与 Boost.Asio 的代理引擎原型，目标是把“接入 → 协议识别 → 路由 → 上游连接 → 双向转发”这条链路跑通，并保留清晰的模块边界，便于后续演进。

你可以把它理解为一个会处理两类客户端的代理入口：

- 普通浏览器/代理客户端：走明文 HTTP，包含 `CONNECT` 场景（HTTPS 代理）。
- 私有客户端（Obscura）：走 WebSocket(SSL) 的“传输封装”，客户端把目标信息藏在握手阶段，再进入加密的双向转发。

### 2. 目录与模块对照

理解仓库最重要的两个目录：

- `include/forward-engine/*`：主要的对外接口与核心实现（大量 header-only）。
- `src/forward-engine/*`：部分模块的 `.cpp` 实现与可执行程序入口。

关键模块（建议按顺序看）：

- `agent/worker.hpp`：监听端口、accept 客户端连接、创建 `session`。
- `agent/session.hpp`：会话生命周期与主链路（协议识别、处理 HTTP/Obscura、建立隧道）。
- `agent/analysis.hpp/.cpp`：协议识别（peek）、目标解析（host/port/正反向判断等）。
- `agent/distributor.hpp/.cpp`：路由与连接获取（正向/反向/直连等策略入口）。
- `agent/connection.hpp/.cpp`：TCP 连接池与复用（缓存、僵尸检测、空闲超时、上限等）。
- `agent/obscura.hpp`：基于 Beast WebSocket(SSL) 的封装，提供 `handshake/async_read/async_write`。
- `agent/adaptation.hpp`：统一不同 socket/stream 的 `async_read/async_write` 适配层。
- `http/*`：HTTP request/response/header 类型与编解码。

### 3. 代理相关核心概念

#### 3.1 正向代理 vs 反向代理

- `正向代理`：客户端告诉代理“我要去哪里”（例如 `CONNECT host:port` 或普通 HTTP 请求行里包含目标信息）。代理负责“帮我连上目标并转发”。
- `反向代理`：客户端认为自己在访问一个固定域名/入口，代理根据内部路由表把请求转发到真实后端（通常需要配置映射表）。

在 ForwardEngine 里，这个分支由 `analysis::resolve` 的结果驱动：会决定走 `route_forward` 还是 `route_reverse`。

#### 3.2 HTTP `CONNECT` 是什么

`CONNECT` 是 HTTPS 代理的常见方式：

- 客户端先发 `CONNECT host:port HTTP/1.1 ...`，请求代理“建立到目标的 TCP 隧道”。
- 代理如果同意，会回 `HTTP/1.1 200 Connection Established\r\n\r\n`。
- 从这之后，双方就把这条 TCP 连接当作“纯字节流隧道”使用；代理不再解析 HTTP，而是做双向搬运。

#### 3.3 “隧道/双向转发”意味着什么

隧道阶段本质就是两个方向各跑一个循环：

- 客户端 → 上游：读客户端字节流，写到上游。
- 上游 → 客户端：读上游字节流，写回客户端。

难点不是“能不能转发”，而是“怎么正确退出”：

- 一边 EOF 了，另一边不能永远卡住。
- 一边异常了，另一边要尽快停工并回收资源。
- 不应把“正常断开”（EOF/取消/连接重置）误判为业务错误。

### 4. Boost.Asio 协程与执行模型（理解代码的关键）

项目大量使用 `net::awaitable<void>` + `co_await` 组织异步流程。

你需要知道几个关键词：

- `executor`：协程恢复执行的上下文（通常来自 `io_context`）。
- `co_spawn`：把一个协程任务投递到事件循环。
- `use_awaitable`：把异步操作转换为 `co_await` 的形式。
- `redirect_error`：把异常风格的错误改为写入 `error_code`（利于区分“正常收尾”与真正错误）。

#### 4.1 为什么要用 `cancellation_signal/slot`

在双向转发时，最常见的坑是：A 方向结束后，B 方向仍在 `co_await` 读写，导致会话无法退出。

ForwardEngine 采用的策略是：

- 为两个方向分别创建 `cancellation_signal`，并把它们的 `slot()` 绑定到各自的读写操作上。
- 任意一边结束（包括 EOF/取消/异常）后，向另一边 `emit` 取消信号，让对向的 `co_await` 尽快返回 `operation_aborted`，从而“优雅停工”。
- `close()` 的职责主要是“回收资源”，而不是用来“打断正在等待的异步读写”。

### 5. Obscura（传输封装）与“伪装层”要点

Obscura 的思路可以粗略理解为：

- 外层表现为 WebSocket(SSL) 的握手与帧传输（基于 Beast）。
- 在握手阶段，客户端把目标信息放到某个约定的字段里（例如 path），服务端从握手结果解析出目标，然后建立到上游的连接。
- 握手完成后，WebSocket 连接在 SSL 之上变成一条“加密的二进制通道”，后续数据以 WebSocket 帧承载，不再出现传统 HTTP 的 header/method/url。

关于“伪装层”常见的现实约束：

- 对抗/伪装通常关注握手阶段可观察信息（例如 `SNI`）与流量特征（包大小、频率、时序等）。
- SSL 建立后内容不可见，但握手阶段的一些信息仍是明文可观察的，所以需要在“握手看起来像什么”与“后续流量像不像正常业务”上做文章。

### 6. 连接池与复用（为什么需要它）

建立 TCP 连接的成本不低（握手、系统资源、延迟）。项目提供了按目标端点缓存空闲连接的复用能力：

- 连接回收：使用 `internal_ptr` + 自定义 `deleter`，在智能指针析构时自动回收到池中。
- 健康检查：包含基础僵尸检测与空闲超时淘汰。
- 上限控制：避免单个端点无限积攒空闲连接。

### 7. 如何验证理解是否正确

最快的验证方式是跑测试并对照主链路：

- `session_test`：启动一个最小代理 + 上游回显/模拟上游断开，然后验证转发与双向退出语义。
- `obscura_test`：验证 Obscura 握手、读写、关闭的基本链路。
- `connection_test`：验证连接复用是否命中。
