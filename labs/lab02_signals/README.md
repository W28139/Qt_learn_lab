# Lab 02：跨线程信号

## 需求

1. 窗口标题 `Lab 02 - Signals & Threads`，约 `560 x 420`。
2. 顶部标签显示 UI 线程 id；只读 `QPlainTextEdit` 日志区，最多 300 行。
3. `Worker` 通过 `moveToThread` 运行在 `QThread`：
   - `start(count, intervalMs)` 在 worker 线程创建定时器，发 `tick(int)`，共 count 次后发 `finished(int)`；
   - `stop()` 停止并记录位置；`message(QString)` 报告关键动作。
4. 三个按钮：开始（100 次 / 50ms）、停止、清空日志。
5. 连接要求：
   - 开始/停止按钮 → worker，context 为 worker（排队到 worker 线程）；
   - `tick` → `onTickQueued`，显式 `Qt::QueuedConnection`，更新进度并每 10 次记一条带线程 id 的日志；
   - `tick` → `onTickDirect`，显式 `Qt::DirectConnection`，只写 `qInfo()`，不碰控件；
   - `message`、`finished` 用 Auto 连接更新日志和状态。
6. 关窗口时 `quit()` + `wait()`；构建 0 warning。

## 可选实验

- 把 queued 改成 Auto，行为应完全相同；
- 把 `onTickQueued` 改成 Direct，观察在 worker 线程操作控件的后果，做完改回；
- 把 tick 改成自定义类型，故意不 `qRegisterMetaType`，观察运行时报错。

## 验收

- [ ] UI 线程 id 与 queued 日志中的线程 id 一致；
- [ ] Direct 日志在 worker 线程执行；
- [ ] 100 次 tick 期间界面全程可响应，停止及时；
- [ ] 完成后状态显示“完成”，运行中关窗不崩溃；
- [ ] 能说明 queued 与 direct 槽分别运行在哪个线程；
- [ ] 0 warning。

参考实现见本目录。