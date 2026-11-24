# Lab 06：TCP 分帧服务端与客户端

## 需求

1. 控制台工程 `lab06_stream`，依赖 `Qt6::Core`、`Qt6::Network`；复用单元 05 的编解码文件，不改协议。
2. `StreamDecoder`：
   - `append()` 追加 `readyRead()` 拿到的任意字节；`nextFrame()` 返回 `NeedMoreData` / `FrameReady` / `ProtocolError`；
   - 头部不足 20 字节时等待，长度非法立即报错，不预分配；
   - magic 错误时逐字节向后重同步，丢弃字节计入 `discardedBytes()`；
   - version/type/length/CRC 错误为致命错误，`clear()` 前一直返回错误；
   - 缓冲上限 1 MiB，不能无限增长。
3. 服务端 `--server`：
   - 监听 `127.0.0.1`，端口由 `--port` 指定，0 表示系统分配；
   - 每个客户端 50ms 发一帧，每帧 2 个样本、4 通道轮换，设备 ID `lab06-server`，序号全局递增；
   - 收到 Heartbeat 回同 sequence 的 Ack；用 `StreamDecoder` 解析客户端字节，协议错误断开；
   - `bytesToWrite()` 超过 256 KiB 时跳过本次发送；
   - 客户端断开时清理定时器和 socket；`--seconds N` 到时退出。
4. 客户端 `--client`：
   - 连接 `--host/--port`（默认 `127.0.0.1:9000`），连上后发 Heartbeat；
   - `readyRead()` 喂 `StreamDecoder`，累计帧数和样本数，每 10 帧打印统计；
   - 收到 `--frames N`（默认 100）帧后打印汇总并正常退出；
   - 断线按 1s、2s、4s 退避重连，最多 3 次，重连前清空解包器；主动停止后不重连；
   - 协议错误不重连，打印原因并以非 0 退出；
   - 默认 30 秒总超时；连接超时 5 秒用 `QTimer`，不用 `waitForConnected()`。

## 可选实验

- 服务端把多帧拼成一次 `write()`，验证客户端不依赖写边界；
- 客户端故意 2 秒不读取，观察服务端 `bytesToWrite()` 和跳过日志；
- 临时发送 version=2 的帧，确认客户端报 `UnsupportedVersion` 后退出而不是重连。

## 验收

- [ ] 两个真实进程跑通，客户端收满 `--frames` 后正常退出，`discarded=0`，无重复/乱序；
- [ ] Heartbeat 能收到同 sequence 的 Ack；
- [ ] 中途关闭服务端后客户端能重连；3 次失败后以非 0 退出；
- [ ] 协议错误导致停止而不是重连；
- [ ] 慢客户端跳过发送，连接断开后连接表和定时器清理干净；
- [ ] 没有在服务端或客户端重复实现编解码；0 warning。

参考实现见本目录。