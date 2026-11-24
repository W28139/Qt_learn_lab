# Lab 06：TCP 遥测流：分帧服务端 + 客户端

> **配套知识**：[单元 06 - Qt Network 与流式解包](../../06-Qt%20Network%20与流式解包.md)
> **工程位置**：`D:\Qt_\learn_demo\lab06_stream\`（Windows，用 Qt Creator 打开）
> **参考实现**：工程内的 `.cpp/.h` 和 `CMakeLists.txt`。先按本文档自己写，写完再对照。

---

## 目标

把单元 05 的编解码器真正接到 TCP 上：

- 服务端持续把 `TelemetryBatch` 编码成帧写进 socket；
- 客户端从 `readyRead()` 拿到的任意字节片段中切出完整帧，再交给 `decodeFrame()`；
- 用真实回环连接验证：发送多少帧就收到多少帧，解码错误为 0；
- 断线能发现，按退避策略重连，协议错误不盲目重连。

## 前置

- 单元 05 完成，`protocol.*`、`crc32.*`、`telemetrytypes.h` 可用；
- 知道 `TelemetryBatch` 的帧布局（知识文档单元 05 第 6 节）；
- 能看懂 Qt 信号槽和事件循环。

## 需求

### 1. 工程

- 工程名 `lab06_stream`，**控制台程序**，依赖 `Qt6::Core` 和 `Qt6::Network`；
- C++20，MSVC 下带 `/utf-8`；
- 控制台程序：CMake 中设置 `WIN32_EXECUTABLE FALSE`；
- 构建 0 warning。

### 2. 复用单元 05 的编解码器

把 `protocol.h/.cpp`、`crc32.h/.cpp`、`telemetrytypes.h` 复制进本工程，**不要修改协议格式**。本 Lab 只新增网络层和分帧层。

### 3. StreamDecoder（本 Lab 的核心）

实现一个有状态的流式解包器：

- `append(QByteArrayView)`：把 `readAll()` 拿到的任意字节追加进内部缓冲；
- `nextFrame()`：返回三态之一：
  - `NeedMoreData`：缓冲里还不够一帧；
  - `FrameReady`：切出一个完整帧，并已通过 `decodeFrame()` 校验；
  - `ProtocolError`：协议错误，当前连接不可继续。

行为要求：

- **先检查、后消费**：长度不合法时不等待、不预分配；
- 头部不足 20 字节时等更多数据；
- magic 不对时逐字节向后找下一个 magic（重同步），把丢弃的字节计入 `discardedBytes()`；
- version / type / payloadLength / CRC 错误都是**致命错误**，不再尝试恢复；
- 声明长度超过协议上限（64 KiB）立即报 `BadLength`，不要等对端把 64 KiB 发完；
- 致命错误后继续调用 `nextFrame()` 仍返回错误，直到显式 `clear()`；
- 缓冲上限 1 MiB，任何情况下不无限增长。

### 4. 服务端

`main --server`：

- `QTcpServer` 监听 `127.0.0.1`，端口由 `--port` 指定（0 表示让系统分配并打印实际端口）；
- 每个客户端一个 50ms 定时器，每次发送一帧 `TelemetryBatch`：
  - 每帧 2 个样本，4 个通道轮换；
  - 设备 ID `lab06-server`；
  - 样本序号全局递增，时间戳取真实当前毫秒；
  - 用单元 05 的 `encodeTelemetryBatch()` 编码；
- 收到客户端的 `Heartbeat` 帧后，回一个同 sequence 的 `Ack` 帧；
- 收到客户端的字节也用 `StreamDecoder` 解析；协议错误则断开该客户端；
- **慢客户端保护**：`socket->bytesToWrite()` 超过 256 KiB 时跳过本次发送并记日志，不允许发送缓冲无限增长；
- 客户端断开时停止定时器、从连接表移除、`deleteLater()` 释放 socket；
- `--seconds N`（N>0）到时停止服务并退出，便于脚本测试。

### 5. 客户端

`main --client`：

- 连接 `--host` / `--port`（默认 `127.0.0.1:9000`）；
- 连上后发一个 `Heartbeat` 帧；
- `readyRead()` 里把字节喂给 `StreamDecoder`，循环取帧：
  - `TelemetryBatch`：用 `decodeTelemetryPayload()` 解出设备 ID 和样本，累计帧数/样本数；
  - `Ack`：打印 `server ack seq=...`；
  - 每 10 帧打印一次统计；
- 达到 `--frames N`（默认 100）后打印汇总并正常退出（退出码 0）；
- **重连**：连接失败或中途断开时，按 1s、2s、4s 退避，最多 3 次；
  - 每次重连前 `clear()` 解包器，旧连接的半帧不能带进新连接；
  - 用户主动停止后不再重连；
  - 协议错误（版本/CRC/长度等）**不重连**，打印原因并以非 0 退出；
- 默认 30 秒总超时，避免测试挂死；`--seconds N` 可覆盖；
- 连接超时（5 秒）用 `QTimer` 实现，不要用阻塞的 `waitForConnected()`。

> 本 Lab 不要求写独立的测试文件。重点放在 **socket 收发 + 分帧**本身：
> 用服务端和客户端两个真实进程跑通、观察日志、手动制造断线和坏帧即可。
> 需要临时验证 `StreamDecoder` 时，可以先在 `main` 里写几行临时代码，验证完删掉。

## 步骤

1. 建工程，复制单元 05 的编解码文件，先让空 `main()` 构建成功；
2. 写 `StreamDecoder`，先用一条完整帧验证能切出一帧（可以临时写在 `main` 里，验证完删掉）；
3. 再用逐字节喂入和任意切分的手工实验确认不会提前出帧，然后删掉临时验证代码；
4. 写服务端：先只监听 + 打印 `newConnection`；
5. 加每客户端定时器和 `encodeTelemetryBatch()`，用客户端模式或临时读端确认能收到字节；
6. 写客户端：先只 `connectToHost` + 打印 `readyRead()` 字节数，确认连接通；
7. 接入 `StreamDecoder` 和 `decodeTelemetryPayload()`，打印帧/样本统计；
8. 加 Heartbeat/Ack 和服务端的客户端帧解析；
9. 加慢客户端保护、断开清理；
10. 加重连和超时；
11. 用两个真实进程做回环验证，打开警告逐条清零；
12. 按验收清单实测：正常、断线重连、服务端重启、协议错误。

## 运行后应该看到

服务端：

```text
[server] listening on 127.0.0.1:9000
server listening on port 9000
[server] client connected: 127.0.0.1:54550
[server] heartbeat seq=1 -> ack
[server] client disconnected, 0 remaining
```

客户端：

```text
[client] connecting to 127.0.0.1:9000
[client] connected, sending heartbeat
[client] server ack seq=1
[client] frames=10 samples=20 lastSeq=9 device=lab06-server
[client] frames=20 samples=40 lastSeq=19 device=lab06-server
...
[client] target reached: 100 frames, 200 samples, discarded=0
```

断线重连（中途杀掉服务端，1 秒后重启）：

```text
[client] socket error: The remote host closed the connection
[client] The remote host closed the connection; reconnecting in 1000 ms (attempt 1/3)
[client] socket error: Connection refused
[client] Connection refused; reconnecting in 2000 ms (attempt 2/3)
[client] connected, sending heartbeat
[client] target reached: 200 frames, 400 samples, discarded=0
```

具体帧数/样本数取决于运行时长，不要求完全一致；关键是 `discarded=0`、没有重复和乱序。

## 可选实验（建议至少做两个）

**实验 A：一次 write 发多帧**

在服务端把多帧拼成一个 `QByteArray` 后一次 `write()`，观察客户端是否仍能逐帧取出。再把一次 `write()` 拆成多次、每次只写一半（中间不加延时），验证客户端不依赖写边界。

**实验 B：制造超慢客户端**

在客户端 `readyRead()` 里故意停 2 秒不读取（或把接收端换成只连接不读的脚本），观察服务端 `bytesToWrite()` 增长和“跳过批次”日志。再调低/调高 256 KiB 阈值，讨论更合理的策略：丢帧、降采样还是断开。

**实验 C：用 netcat / Python 观察字节流**

用 `nc 127.0.0.1 9000 | xxd` 或 Python 脚本连接服务端，确认看到的是连续字节流，帧之间没有分隔符；再按 20 字节头手工解析长度和 CRC，验证“边界只能靠长度字段”。

**实验 D：协议错误不重连**

临时让服务端发送 version=2 的帧（或客户端把服务端第一帧改坏），确认客户端报 `UnsupportedVersion`/`BadChecksum` 后直接退出，而不是陷入重连循环。想一想：哪些错误可以重连，哪些不能？

**实验 E：连接数上限与清理**

写脚本同时建立 20 个连接，断开其中一半，观察 `clientCount()` 和定时器数量是否归零；给服务端加最大连接数限制，超过时拒绝并关闭。用 `QObject::findChildren<QTimer *>()` 检查有没有泄漏的定时器。

**实验 F：心跳与读超时**

目前只有连接超时。给客户端加“10 秒没收到任何帧就判定链路假死并重连”的读超时（用 `QTimer` 每次收到数据后重新计时），讨论 TCP keep-alive 与应用层心跳的分工。

**实验 G：IPv6 与 `QHostAddress::Any`**

把监听地址改成 `QHostAddress::Any`，用 `::1` 和 `127.0.0.1` 分别连接，观察 Windows 防火墙提示和 `peerAddress()` 的差异。不要在公网环境做这个实验。

## 验收清单

- [ ] 构建成功，0 warning；
- [ ] 服务端与客户端两个真实进程能跑通，客户端达到 `--frames` 后正常退出；
- [ ] 正常路径：服务端 20Hz 发送，客户端 100 帧后正常退出，`discarded=0`；
- [ ] 客户端收到的帧数与样本数符合发送速率，没有重复帧或乱序；
- [ ] `Heartbeat` 能收到对应 sequence 的 `Ack`；
- [ ] 客户端在服务端中途关闭后能重连，重启服务端后继续收到数据；
- [ ] 重连时解包器已清空，新连接第一帧能正常解码；
- [ ] 3 次重连都失败后客户端以非 0 退出，不无限重试；
- [ ] 协议错误（版本/CRC/长度）导致客户端停止而不是重连；
- [ ] 服务端 `bytesToWrite()` 超过阈值时跳过批次，不无限增长；
- [ ] 客户端断开后服务端连接表和定时器清理干净（`clientCount()==0`）；
- [ ] 所有多字节字段仍按单元 05 的大端格式，没有在服务端或客户端重复实现编解码；
- [ ] 能解释“为什么 `readAll()` 一次返回的内容不等于一帧”；
- [ ] 能解释“为什么 magic 错误可以重同步，CRC 错误不能”。

## 思考

1. TCP 保证可靠、有序，为什么还需要应用层帧头和长度字段？没有长度字段，接收方还能靠什么分帧？
2. 服务端一次 `write()` 一帧，客户端就一定能一次 `readyRead()` 收到一帧吗？在哪些情况下不是？
3. `StreamDecoder` 为什么必须“先检查长度、再等完整帧、最后才消费”？如果先 `remove(0, total)` 再校验，会丢什么？
4. magic 重同步为什么可以逐字节丢弃？如果载荷里刚好出现 `0x54 0x4C`，会不会误判？后面的哪些校验能兜底？
5. 为什么 version 不认识、CRC 不对时不应该重连？如果对端升级了协议，客户端应该怎么表现？
6. 服务端为什么要看 `bytesToWrite()`？只调用 `write()` 不管返回值，会出什么问题？
7. 客户端重连时为什么必须清空解包器？不清空可能把什么旧数据拼进新连接？
8. 为什么重连要退避，而不是每 10ms 重试一次？在大量客户端同时掉线的场景下，退避和随机抖动各解决什么问题？
9. 为什么连接超时用 `QTimer`，而不是 `waitForConnected(5000)`？在 GUI 线程里阻塞会有什么后果？
10. 如果把 `QTcpSocket` 移到工作线程（单元 07），`readyRead` 会在哪个线程执行？解包器和模型分别应该在哪？
11. 服务端为每个客户端创建一个 `QTimer`，1000 个客户端会有什么开销？有没有更省的调度方式？
12. 如果协议允许“一条消息跨多个帧”，本 Lab 的分帧层需要改哪里？如果允许“一个帧里多条消息”，又该改哪里？

---

## 模块关系

```text
main.cpp
  ├── --server → server.cpp (FrameServer)
  │                ├── QTcpServer       接受连接
  │                ├── 每客户端 QTimer  周期发送
  │                └── StreamDecoder    解析客户端发来的 Heartbeat
  ├── --client → client.cpp (FrameClient)
  │                ├── QTcpSocket       连接/重连
  │                ├── StreamDecoder    切帧
  │                └── decodeTelemetryPayload()  解样本

共享：protocol.* / crc32.* / telemetrytypes.h（来自单元 05，不修改）
```

关键点：

- 网络层只负责“连接、收发字节”；分帧层只负责“从字节流切出帧”；编解码层只负责“帧 ↔ 数据结构”；
- 三层之间用 `QByteArray` / `StreamDecoder` 的返回值连接，没有全局状态；
- `StreamDecoder` 不依赖 socket，可以单独测试；
- 单元 07 会把 `FrameClient` 和 `StreamDecoder` 整体搬进工作线程，界面只接收解好的样本。

## 参考实现说明

工程内含 `main.cpp`、`streamdecoder.h/.cpp`、`server.h/.cpp`、`client.h/.cpp`，并复制了单元 05 的 `protocol.h/.cpp`、`crc32.h/.cpp`、`telemetrytypes.h`。本 Lab 不包含自动化测试文件，验证靠两个真实进程。

参考实现已在 Qt 6.4.2 + GCC 下用 `-Wall -Wextra` 构建通过（0 warning）。用两个真实进程验证：50 帧正常收包、`discarded=0`；以及“杀掉服务端 → 1 秒后重启 → 客户端重连 → 收满 200 帧”。本工程没有测试文件，验证靠直接运行服务端和客户端。

建议顺序：先自己写完并过验收清单，再打开对照。重点看 `StreamDecoder::nextFrame()` 的顺序、服务端断开清理和客户端重连时清空缓冲。

## 完成后

按 [README](../../README.md) 的规则回复“**单元 6 完成**”，并附上：

```text
单元 6 完成
- 构建是否 0 warning：
- 正常收包帧数 / 样本数：
- 断线重连是否成功：
- 协议错误是否停止而非重连：
- 自测题有没有不会的：
```