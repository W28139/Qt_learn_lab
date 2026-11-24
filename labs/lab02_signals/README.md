# Lab 02：跨线程信号实验台

> **配套知识**：[单元 02 - 信号槽与跨线程语义](../../02-信号槽与跨线程语义.md)
> **工程位置**：`D:\Qt_\learn_demo\lab02_signals\`（Windows，用 Qt Creator 打开）
> **参考实现**：工程内的 `.cpp/.h` 和 `CMakeLists.txt`。先按本文档自己写，写完再对照。

---

## 需求

在 `D:\Qt_\learn_demo\lab02_signals\` 新建独立 CMake 工程，实现一个窗口：

1. 标题 `Lab 02 - Signals & Threads`，大小约 `560 x 420`；
2. 顶部 `QLabel` 显示 **UI 线程标识**（用 `QThread::currentThread()` 的地址即可）；
3. 一个只读 `QPlainTextEdit` 作为日志区，最多保留 300 行；
4. 一个 worker 对象，`moveToThread` 到一个 `QThread`：
   - `start(int count, int intervalMs)`：在 worker 线程创建/复用 `QTimer`，按间隔产生 `tick(int)` 信号，共 `count` 次，然后发 `finished(int total)`；
   - `stop()`：停止计时器并记录当前位置；
   - `message(QString)`：报告关键动作，便于观察线程；
5. 三个按钮：**开始（100 次 / 50ms）**、**停止**、**清空日志**；
6. 连接要求：
   - “开始/停止”按钮 → worker 的 lambda，**context 是 worker**（体验 AutoConnection 排队到 worker 线程）；
   - `Worker::tick` → 窗口 `onTickQueued`，**显式 `Qt::QueuedConnection`**，更新进度标签并每 10 次记一条带线程 id 的日志；
   - `Worker::tick` → 窗口 `onTickDirect`，**显式 `Qt::DirectConnection`**；这个槽**只写 `qInfo()` 日志，绝不碰任何控件**；
   - `Worker::message` → 窗口 `onWorkerMessage`（Auto，跨线程自动排队）；
   - `Worker::finished` → 带 context 的 lambda，更新状态标签；
7. 关闭窗口时 `quit()` + `wait()` 线程；
8. 构建 0 warning。

## 步骤

1. 按单元 00/01 的方式建工程，`CMakeLists.txt` 只依赖 `Widgets`（`QThread` 在 `Core` 里，Widgets 已经带上）；
2. 先写 `Worker` 类，注意 `Q_OBJECT` 和文件末尾的 `#include "main.moc"`；
3. 写窗口和线程装配，按上面的连接要求逐条 `connect`；
4. 构建、运行，打开 Qt Creator 的“应用程序输出”，观察两类日志的线程 id；
5. 完成验收清单后再对照参考实现。

## 运行后应该看到

- 点击“开始”后，进度标签大约每 50ms 变一次，共 100 次；
- 点击“停止”立即生效，日志显示停在哪个值；
- UI 日志里的 queued 槽线程 id，与顶部“UI 线程”一致；
- 应用程序输出里的 Direct 日志线程 id，与 UI 线程**不同**（是 worker 线程）；
- 拖动窗口、点“清空日志”时，计数仍在继续——界面没有被阻塞；
- 运行中直接关窗口，不崩溃、无警告。

## 可选实验（建议至少做前两个）

**实验 A：Auto 与 Queued 等价**

把 `onTickQueued` 的显式 `Qt::QueuedConnection` 改成 `Qt::AutoConnection`（或不写），重新运行。行为应该完全一样。这验证了“worker → UI 的 Auto 就是 Queued”。

**实验 B：Direct 更新 UI 的后果**

临时把 `onTickQueued` 的连接类型改成 `Qt::DirectConnection`，运行并开始工作。此时槽在 worker 线程里改 `QLabel`。观察程序行为（可能无异常、可能错乱、可能崩溃——这本身就是“未定义行为”的体现）。**做完立刻改回来。**

**实验 C：自定义类型跨线程**

把 `tick(int)` 改成 `tick(Telemetry)`（`Q_DECLARE_METATYPE` + `qRegisterMetaType`）。先故意不注册，观察运行时报错，再注册并验证。这个实验能让你以后看到 `Cannot queue arguments` 时立刻知道原因。

**实验 D：重复连接**

不修改代码，思考：如果在构造函数末尾再执行一次相同的 `connect(m_worker, &Worker::tick, this, &SignalWindow::onTickQueued, Qt::QueuedConnection);`，会发生什么？如何用 `Qt::UniqueConnection` 避免？

## 验收清单

- [ ] 构建成功，0 warning；
- [ ] 顶部 UI 线程 id 与 queued 日志里的线程 id 一致；
- [ ] Direct 连接的日志显示它在 worker 线程执行；
- [ ] 点击开始后约 100 次 tick，界面全程可响应；
- [ ] 停止按钮能及时停止，并记录停止位置；
- [ ] 自然完成后状态标签显示“完成”；
- [ ] 运行中关闭窗口不崩溃；
- [ ] 能口头解释：为什么 queued 槽在 UI 线程，而 direct 槽在 worker 线程；
- [ ] （可选）完成实验 A、B、C 中的至少两个。

## 思考

1. 如果把 `onTickQueued` 改成 Direct，程序可能不崩溃但结果是错的，为什么“看起来能跑”比直接崩溃更危险？
2. “开始”按钮的 lambda 为什么可以直接调用 `m_worker->start(...)`，而你在 UI 线程里直接调用它就不行？
3. 关闭窗口时，如果不调用 `wait()` 只调用 `quit()`，可能发生什么？
4. worker 线程如果没有事件循环（例如子类化了 `QThread` 并重写 `run()` 却不调用 `exec()`），queued 槽会怎样？
5. 如果 worker 每秒产生 1000 个 tick，这个 Lab 的结构会先在哪里出问题？

---

**参考实现**：工程内的 `main.cpp`。完成后先自查验收清单，再打开对照。