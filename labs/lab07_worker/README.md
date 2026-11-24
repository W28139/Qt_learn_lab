# Lab 07：网络 worker、线程安全队列与优雅退出

> **配套知识**：[单元 07 - QThread 与工作线程模型](../../07-QThread与工作线程模型.md)
> **工程位置**：`D:\Qt_\learn_demo\lab07_worker\`（Windows，用 Qt Creator 打开）
> **参考实现**：工程内的 `.cpp/.h` 和 `CMakeLists.txt`。先按本文档自己写，写完再对照。

---

## 目标

把单元 06 的客户端从 GUI 线程整体搬到工作线程：

- `QTcpSocket`、`StreamDecoder`、解码和重连全部在 worker 线程；
- worker 把解好的 `TelemetryBatch` 放进**有界的线程安全队列**；
- GUI 线程按固定频率取队列、批量更新表格；
- 关闭窗口/停止时，按正确顺序停止网络、退出线程、回收对象，不崩不卡。

这个 Lab 的重点不是“能收到数据”，而是**线程边界是否干净**：谁创建对象、谁执行槽、谁访问模型、谁负责删除。

## 前置

- 单元 05：`protocol.*`、`crc32.*`、`telemetrytypes.h` 可用；
- 单元 06：能在主线程用 `QTcpSocket` + `StreamDecoder` 收帧；
- 单元 02：理解 `AutoConnection`、队列连接和线程亲和性；
- 单元 04：知道模型要批量 `append`，不要逐条刷新。

## 需求

### 1. 工程

- 工程名 `lab07_worker`，**GUI 程序**，依赖 `Qt6::Core`、`Qt6::Network`、`Qt6::Widgets`；
- 控制台程序：CMake 中设置 `WIN32_EXECUTABLE FALSE`，保留 worker 日志输出；
- C++20，MSVC 下带 `/utf-8`；
- 构建 0 warning；
- 复用单元 05/06 的协议、CRC、`StreamDecoder`，**不要修改线上格式**。

### 2. 线程安全的有界批次队列

实现一个生产者-消费者队列：

- 存 `TelemetryBatch`：帧序号、设备 ID、样本列表；
- `push()` 由 worker 线程调用，`takeAll()` 由 GUI 线程调用；
- 容量固定（参考值 64 批）；
- **满了丢最旧的一批**，生产者不阻塞；
- 所有访问都加锁，包括统计；
- 提供只读统计：当前深度、容量、历史峰值、累计入队、累计丢弃、累计消费批次/样本；
- `takeAll()` 返回副本并清空队列；没有数据时返回空列表。

验收时能回答：为什么选“丢最旧”而不是“阻塞生产者”？峰值和丢弃数说明什么？

### 3. worker 对象（`TelemetryWorker`）

用 `moveToThread` 模式，不要继承 `QThread`：

- 构造时只保存 host/port/队列指针，**不创建 socket/timer**；
- `start()` 槽在 worker 线程执行时才创建 `QTcpSocket`、重连 `QTimer`、`StreamDecoder`；
- 连上后发一个 `Heartbeat`（服务端可忽略）；
- `readyRead` 里：`readAll()` → 解包循环 → 只处理 `TelemetryBatch` → 解码 → 构造批次 → `queue->push()`；
- 协议错误（版本/CRC/长度/载荷非法）是**致命**：停止、报告 Failed、**不重连**；
- 连接失败/断开：按 1 s、2 s、4 s 退避重连，最多 3 次，之后进入 Failed；重连前清空解包器；
- 统计信号（帧数/样本数/重连次数）**最多每 250 ms 发一次**，不要每帧发；
- 状态信号：待机/连接中/已连接/重连中/失败/已停止，附带可读 detail；
- 额外的 `workerThreadId(quintptr)` 信号，让 GUI 能显示真实的工作线程 id；
- `stop()` 幂等：停定时器、abort/释放 socket、释放解包器、置停止标志，之后不再安排重连；
- 析构函数兜底清理，不留下活动 socket/timer。

注意：所有 TCP 操作都必须发生在 socket 的归属线程；不要在 GUI 线程直接调用 worker 的方法。

### 4. 数据源（mock server）

默认模式不需要第二个进程：

- 在**主线程**创建 `MockTelemetryServer`，监听 `127.0.0.1`，端口用 0 让系统分配；
- 每个客户端 50 ms 发一帧（20 Hz），每帧 2 个样本，4 个通道轮换；
- 帧里用单元 05 的 `encodeTelemetryBatch()`；
- 可选：第 N 帧故意翻转 CRC，用于验证 worker 的协议错误处理；
- 客户端断开时停定时器、从连接表移除、`deleteLater()`；
- 慢客户端保护：`bytesToWrite()` 超过 256 KiB 时跳过本周期。

也允许 `--host/--port` 连接外部服务端（例如单元 06 的 `lab06_stream --server`）。

### 5. GUI

主窗口至少包含：

- `QTableView` + 只读模型（可以复用单元 04 的模型/环形缓冲，容量 2000）；
- 按钮：开始、停止、清空表格；
- 状态文字：连接状态 + detail；
- 统计文字：帧数、样本数、重连次数；
- 队列文字：当前深度/容量、历史峰值、丢弃批次；
- 线程文字：GUI 线程 id、worker 线程 id、GUI 已消费样本数。

行为要求：

- 开始：新建 `QThread` + worker，`moveToThread`，通过 `thread.started` 触发 `start()`；
- GUI 用 25～30 Hz 的定时器 `takeAll()`，展开样本后**一次** `appendSamples()`；
- 停止：队列连接调用 `stop()`，再 `quit()`，不要 `wait()` 阻塞 GUI；
- 线程 `finished` 后：清理线程对象、恢复按钮、状态显示“已停止”；
- 关闭窗口：先停 worker、`quit()`、`wait(3000)`；超时要记录警告；
- worker 的删除用 `finished → deleteLater`，停止后不要再访问 worker 指针；
- 模型只允许在 GUI 线程更新。

### 6. 命令行

参考实现提供三种运行方式：

- 默认：GUI + 内嵌 mock server；
- `--host H --port P`：GUI + 外部服务端；
- `--auto-seconds N`：GUI 运行 N 秒后自动关窗（用于脚本/无头运行）。

## 步骤

1. 建工程，复制单元 05/06 的协议、CRC、解包器，先让空窗口跑起来；
2. 写队列，先只做 FIFO 和容量，再补丢最旧和统计；
3. 写 `MockTelemetryServer`，先用一个临时客户端确认能收到字节；
4. 写 `TelemetryWorker`：先只连接、打印 `readyRead`，确认在 worker 线程；
5. 接入解包/解码/入队，GUI 定时器取出并显示；
6. 加状态、统计信号和线程 id 显示；
7. 加重连：退避、次数上限、重连前清空解包器；
8. 加协议错误处理：Failed、不重连；
9. 加停止与关窗流程，反复启动/停止，观察是否崩溃或挂起；
10. 打开警告逐条清零，按验收清单实测。

## 运行后应该看到

GUI（控制台输出，10 秒内关闭）：

```text
[mock] mock server listening on 127.0.0.1:37813
[worker] worker started, connecting to 127.0.0.1:37813
[worker] connected, peer=127.0.0.1:37813
[mock] client connected: 127.0.0.1:60652
[gui] shutdown complete: gui_consumed=80 queue_dropped=0 queue_peak=2
[worker] worker stopped by request
[mock] client disconnected, 0 remaining
```

界面上：

- 表格每 40 ms 左右批量插入新样本，滚动跟随最新；
- “帧/样本/重连”大约每 250 ms 更新一次；
- “队列 x/64 | 峰值 y | 丢弃批次 z”反映消费压力；
- GUI 线程 id 和 worker 线程 id 不同；
- 拖动窗口、点清空、点停止，界面都立即响应。

帧数/样本数取决于运行时长，不要求完全一致；重点是界面保持响应、队列统计合理。

## 可选实验（建议至少做两个）

**实验 A：容量对行为的影响**

把队列容量从 64 改成 8、再改成 256，分别运行 10 秒，记录峰值和丢弃数。思考：容量由什么决定？丢弃数长期大于 0 说明什么？

**实验 B：退回“每帧一个信号”**

临时去掉队列，worker 每解出一帧就 `emit` 一个信号给 GUI。观察 GUI 事件队列、内存、响应速度，再换回队列。比较两种做法的延迟和数据新鲜度。

**实验 C：制造慢消费者**

在 GUI 的取队列槽里故意忙等 300 ms（只做实验），观察队列峰值、丢弃数、界面卡顿和服务端 `bytesToWrite()`。再恢复后确认数据能追上最新。

**实验 D：制造一次死锁（限时、只做实验）**

在实验里临时让 GUI 线程用 `BlockingQueuedConnection` 调 worker，同时 worker 用 `BlockingQueuedConnection` 回调 GUI，外部加 5 秒超时。观察程序挂住，然后恢复代码。用一句话说明为什么 GUI 线程不能阻塞等 worker。

**实验 E：继承 QThread 的版本**

把 worker 改成继承 `QThread`、重写 `run()`，在 `run()` 里直接跑一个阻塞循环。对比两种写法：socket 信号、定时器、停止流程分别需要怎么改？为什么本 Lab 不推荐这种写法？

**实验 F：两个 worker**

同时启动两个 `TelemetryWorker`（两个线程、两个队列，或同一个队列），连接同一个 mock server。观察统计、线程 id、停止顺序；讨论共享队列需要哪些额外同步，worker 之间要不要共享 socket。

**实验 G：测量停止耗时**

用 `QElapsedTimer` 记录从点“停止”到 `finished` 的时间，连续 20 次。如果某次明显变慢，检查 worker 里是否有阻塞调用、重连定时器是否没被取消、是否在等 GUI。

## 验收清单

- [ ] 构建成功，0 warning；
- [ ] 默认模式与 `--auto-seconds` 都能正常收数并干净退出；
- [ ] GUI 默认使用内嵌 mock server 就能收到数据，不需要额外进程；
- [ ] `QThread` + worker 使用 `moveToThread`，没有继承 `QThread` 来跑事件循环；
- [ ] socket、重连定时器、解包器在 worker 线程创建；
- [ ] GUI 线程 id 与 worker 线程 id 不同，界面能显示两者；
- [ ] worker 收包期间 GUI 定时器照常触发，窗口可拖动、按钮可点击；
- [ ] 队列有固定容量，满了丢最旧，峰值不超过容量；
- [ ] 模型只在 GUI 线程更新，worker 只往队列里放数据；
- [ ] 统计信号有节流，不是每帧发一次；
- [ ] 断线后按 1 s/2 s/4 s 退避，最多 3 次；重连前清空解包器；
- [ ] 协议错误进入 Failed，**不**进入重连；
- [ ] 停止后线程在超时内结束，按钮恢复，可以再次开始；
- [ ] 连续启动/停止至少 5 次，无崩溃、无挂起、无警告；
- [ ] 关闭窗口时先停 worker 再退出，控制台无 “QThread: Destroyed while thread is still running”；
- [ ] 能解释“为什么 worker 里创建的 socket 不能在 GUI 线程操作”；
- [ ] 能解释“为什么 GUI 线程里不能 `wait()` 一个正在用阻塞连接等 GUI 的 worker”。

## 思考

1. `QThread` 对象本身属于哪个线程？`moveToThread` 移动的是谁？`start()` 之后哪段代码在新线程运行？
2. 为什么说“在 GUI 线程 `new QTcpSocket` 再移动”容易出错？本 Lab 为什么选择在 worker 的 `start()` 里创建？
3. worker 的 `start()` 如果被 GUI 线程直接调用，socket 的归属线程会是谁？`readyRead` 会在哪执行？
4. 队列满时为什么丢最旧而不是阻塞？如果生产者是磁盘写入线程，还会这样选吗？
5. 队列的统计为什么也要在同一把锁里更新？分开统计会出现什么窗口？
6. 为什么统计信号要节流到 250 ms？每帧发一次在 1000 Hz 时会怎样？
7. 为什么模型只能在 GUI 线程更新？worker 直接改模型可能造成什么现象？
8. `finished → deleteLater` 为什么比在 GUI 线程直接 `delete worker` 安全？
9. 停止顺序为什么必须是 stop → quit → wait？把 quit 放最前会漏掉什么？
10. GUI 线程中 `wait()` 和 worker 的阻塞连接如何形成死锁？如何从设计上避免？
11. 协议错误为什么不重连？如果对端升级了协议，客户端应该怎么表现？
12. 队列容量、GUI 刷新频率、网络帧率三者之间是什么关系？怎样用指标判断容量是否合理？

---

## 模块关系

```text
main.cpp
  ├── 默认 → main 线程创建 MockTelemetryServer（20 Hz）
  ├── --host/--port → 连接外部服务端
  └── MainWindow
        ├── GUI 线程：QTimer(40 ms) → queue.takeAll() → TelemetryModel
        ├── QThread m_thread
        │     └── TelemetryWorker（moveToThread）
        │           ├── QTcpSocket / StreamDecoder / 重连定时器
        │           └── queue.push(TelemetryBatch)
        └── queue：BoundedBatchQueue（QMutex，容量 64，drop-oldest）

共享：protocol.* / crc32.* / streamdecoder.* / telemetrytypes.h（来自 05、06，不修改）
      telemetrymodel.* / ringbuffer.h（来自 04，继续在 GUI 线程使用）
```

关键点：

- 线程边界只有两个：`queue.push()` 和 `queue.takeAll()`；
- worker 不持有模型，GUI 不持有 socket；
- mock server 在主线程只负责“供数”，不参与 worker 线程模型；
- 停止时先取消生产，再退出事件循环，最后回收对象。

## 参考实现说明

工程内含 `main.cpp`、`mainwindow.h/.cpp`、`telemetryworker.h/.cpp`、`boundedqueue.h/.cpp`、`mocktelemetryserver.h/.cpp`、`telemetrymodel.h/.cpp`、`ringbuffer.h`、`streamdecoder.*`、`protocol.*`、`crc32.*`、`telemetrytypes.h`。

参考实现已在 Qt 6.4.2 + GCC 下用 `-Wall -Wextra` 构建通过（0 warning），并用 GUI 自动关闭（`--auto-seconds`）确认：

- worker 连接内嵌 mock server 并解码；
- GUI 消费样本，`dropped=0`；
- 关窗时 worker 线程在超时内结束，退出码 0；
- mock server 的连接清理到 0。

建议顺序：先自己写完并过验收清单，再打开对照。重点看 worker 的 `start()`/`stop()`、队列 `push()` 的丢弃策略、`MainWindow` 的启动/停止顺序。

## 完成后

按 [README](../../README.md) 的规则回复“**单元 7 完成**”，并附上：

```text
单元 7 完成
- 构建是否 0 warning：
- GUI 是否保持响应（拖动/按钮）：
- 连续启动/停止次数，是否全部干净退出：
- 断线重连是否成功：
- 协议错误是否停止而非重连：
- 自测题有没有不会的：
```