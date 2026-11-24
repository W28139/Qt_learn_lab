# Lab 07：网络 worker 与线程安全队列

## 需求

1. GUI 工程 `lab07_worker`，依赖 `Qt6::Core`、`Qt6::Network`、`Qt6::Widgets`；复用 05/06 的编解码和解包器。
2. 有界批次队列（容量 64）：
   - 存 `TelemetryBatch`；`push()` 由 worker 调用，`takeAll()` 由 GUI 调用；
   - 满时丢最旧的一批，生产者不阻塞；
   - 所有访问加锁，提供深度、峰值、累计入队/丢弃/消费统计。
3. `TelemetryWorker`：
   - 用 `moveToThread`，不继承 `QThread`；
   - 构造时只保存 host/port/队列指针；`start()` 在 worker 线程创建 socket、重连定时器和 `StreamDecoder`；
   - 连上后发 Heartbeat；`readyRead` 中解包、解码，把批次 `push()` 进队列；
   - 协议错误进入 Failed，不重连；连接失败按 1/2/4s 退避，最多 3 次，重连前清空解包器；
   - 统计信号最多 250ms 一次；状态信号覆盖待机/连接中/已连接/重连中/失败/已停止；发 `workerThreadId`；
   - `stop()` 幂等，析构函数兜底清理。
4. 数据源：默认在主线程创建 `MockTelemetryServer`，监听 `127.0.0.1` 端口 0；50ms 一帧，每帧 2 个样本、4 通道轮换；慢客户端超过 256 KiB 跳过。
5. GUI：
   - `QTableView` + 只读模型（复用单元 04，容量 2000）；开始/停止/清空按钮；
   - 显示连接状态、帧/样本/重连数、队列深度/峰值/丢弃、GUI 与 worker 线程 id；
   - 开始：新建线程和 worker，`started → start()`；
   - 30Hz 定时 `takeAll()`，展开后一次 `appendSamples()`；
   - 停止：通过队列连接调 `stop()`，再 `quit()`，不阻塞 GUI；
   - 关窗：stop → quit → `wait(3000)`；线程 `finished → deleteLater`。
6. 命令行：默认 GUI + 内嵌 mock；`--host/--port` 连外部服务端；`--auto-seconds N` 运行 N 秒后自动关窗。

## 验收

- [ ] 默认模式和 `--auto-seconds` 都能收数并干净退出；0 warning；
- [ ] socket、定时器、解包器在 worker 线程创建；界面能显示两个不同的线程 id；
- [ ] 收包期间窗口可拖动、按钮可点；
- [ ] 队列有固定容量，峰值不超过容量，满时丢最旧；
- [ ] 模型只在 GUI 线程更新；统计信号有节流；
- [ ] 断线按退避重连，协议错误不重连；
- [ ] 连续启动/停止 5 次无崩溃、无挂起；关窗无 QThread 警告。

参考实现见本目录。