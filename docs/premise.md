### 伪装层
- GFW 监听的是 SSL 握手阶段的明文信息 (SNI) 和 流量特征 （比如数据包的大小、频率、时序）
- 需要知道目标服务器的 SNI 和 流量特征 才能进行伪装
- 在建立ssl握手之后数据就完全隐藏了，但是握手阶段的信息是明文的，所以需要伪装成正常的ssl握手
- 一旦 WebSocket 握手成功（状态码 101 Switching Protocols），websocket连接就变成了 纯粹的二进制流通道，在套上ssl加密后，就可以进行正常的通信了
- 在建立微博socket连接之后发送的所有数据都是 WebSocket 帧 (Frames) ，不再有 HTTP 头（Header）、Method、URL 这些东西了