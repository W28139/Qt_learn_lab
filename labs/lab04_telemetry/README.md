# Lab 04：100Hz 遥测表格

> **配套知识**：[单元 04 - Model/View 与高频刷新](../../04-ModelView与高频刷新.md)
> **工程位置**：`D:\Qt_\learn_demo\lab04_telemetry\`（Windows，用 Qt Creator 打开）
> **参考实现**：工程内的 `.cpp/.h` 和 `CMakeLists.txt`。先按本文档自己写，写完再对照。

---

## 需求

在 `D:\Qt_\learn_demo\lab04_telemetry\` 新建独立 CMake 工程，实现一个能持续接收模拟遥测数据的表格页。

1. **工程**
   - 工程名 `lab04_telemetry`，只依赖 Qt6 `Widgets`；
   - C++20，MSVC 下带 `/utf-8`；
   - 构建 0 warning。

2. **数据结构**
   - `TelemetrySample` 至少包含：`qint64 timestampMs`、`quint64 seq`、`quint16 channel`、`double value`、`Quality quality`（`Good` / `Warning` / `Bad`）；
   - 实现模板 `RingBuffer<T>`：固定容量，提供 `pushBack()`、`dropFront()`、`at(int)`、`size()`、`capacity()`、`clear()`；
   - `at(0)` 是最旧元素，`at(size()-1)` 是最新元素；追加和淘汰都是 O(1)，运行期不扩容。

3. **模型**
   - `TelemetryModel : QAbstractTableModel`，五列：序号 / 时间 / 通道 / 数值 / 状态；
   - 只读；`flags()` 不返回 `ItemIsEditable`；
   - `DisplayRole`：序号右对齐、时间 `HH:mm:ss.zzz`、通道 `CH1..CH4`、数值保留 3 位小数、状态显示中文；
   - `TextAlignmentRole`：序号/时间/数值右对齐，其余左对齐；
   - `ForegroundRole`：`Warning` 用橙色，`Bad` 用红色，`Good` 用默认色；
   - 提供至少两个自定义 role（例如原始数值、原始状态码）；
   - `appendSamples(const QList<TelemetrySample> &)`：
     - 一批超过容量时只保留最新 `capacity` 条；
     - 溢出时先 `beginRemoveRows(0, overflow-1)` → `dropFront()` → `endRemoveRows()`；
     - 再 `beginInsertRows(first, first+count-1)` → `pushBack()` → `endInsertRows()`；
     - **不得使用 beginResetModel / endResetModel**；
   - `clear()` 使用 reset，清空缓冲和总数。

4. **模拟数据**
   - 4 个通道，每通道 100Hz；生成定时器 10ms，每次生成 4 条样本（同一时间戳）；
   - 序号全局递增；
   - 数值 = `50 + 12 * sin(phase + channel * 0.8) + noise`，`noise` 在 `[-1, 1]`；
   - 用确定性规则制造少量告警/异常（例如 `seq % 113 == 0` 为 Warning，`seq % 997 == 0` 为 Bad），方便观察颜色；
   - 采集频率和时间戳不要互相“推算”，时间戳取真实当前毫秒。

5. **批量刷新**
   - 生成的数据先放进 `m_pending`；
   - 单独的 flush 定时器每 100ms 调用一次 `model->appendSamples(m_pending)`，然后清空 pending；
   - 不允许每生成一条就调模型。

6. **界面**
   - `QMainWindow` 标题 `Lab 04 - Telemetry Model/View`，初始 `1000 × 640`；
   - 顶部一行：开始采集/停止采集（同一个按钮切换）、清空、`自动跟随最新`（默认勾选）；
   - 顶部右侧显示：实际速率（样本/秒）、缓冲占用（当前/容量）；
   - 中央：`QTableView`；
   - 状态栏：永久标签显示总样本数，临时消息显示“100 Hz × 4 通道 · 每 100 ms 批量刷新”；
   - 表格配置：只读、行选择、单行选择、隔行变色、不换行、隐藏垂直表头、固定行高、等宽字体、数值列拉伸；
   - 缓冲容量 5000。

7. **行为**
   - 点“开始采集”：两个定时器启动，按钮变“停止采集”；
   - 点“停止采集”：停止生成和 flush，把 pending 中最后一批提交，速率归零；
   - 点“清空”：清空模型、pending、序号、计数器和速率；运行中清空后继续从 0 开始；
   - 勾选“自动跟随最新”时，每次 flush 后滚到最后一行；取消勾选后，新数据仍进入表格，但视图不抢滚动条；
   - 运行 15 秒以上：缓冲稳定在 5000 行，不继续增长，表格持续滚动，旧行被淘汰；
   - 反复开始/停止/清空、快速拉伸窗口，无崩溃、无 Qt 警告输出。

## 步骤

1. 按 00/03 的方式建工程，`CMakeLists.txt` 包含 `main.cpp`、`telemetrymodel.cpp/.h`、`ringbuffer.h`、`telemetrytypes.h`；
2. 先写 `RingBuffer`，用一个临时 `main` 或断点验证 `at()` 的顺序；
3. 写 `TelemetryModel`，先只实现 `rowCount/columnCount/data/headerData/flags`，用少量手工样本让空表显示出来；
4. 实现 `appendSamples` 的“无溢出”路径（只 insert），确认表格能持续追加；
5. 把容量调小（例如 20），观察头部淘汰和 remove 信号，确认行数稳定、滚动不跳顶；
6. 写生成器、pending 和 flush 定时器，先用 1Hz 或 10Hz 慢慢调通；
7. 把生成频率提到 100Hz × 4，观察 CPU、速率和界面响应；
8. 加顶部控件、状态栏、自动跟随和清空；
9. 逐项过验收清单。

## 运行后应该看到

- 启动后表格为空，按钮显示“开始采集”；
- 点击开始，约半秒后表格快速出现 `CH1..CH4` 交替的行，自动滚到最新；
- 顶部速率约为 400 样本/秒（受系统定时器精度影响，允许一定偏差）；
- 状态栏总样本数持续增长；
- 约 12～13 秒后缓冲到达 `5000 / 5000`，行数不再增长，表格仍然滚动，最旧的行被新行取代；
- 橙色的“警告”和红色的“异常”偶尔出现；
- 取消“自动跟随最新”后，新行仍在追加，但视图停在当前位置；
- 点“停止采集”：数据立即停止增长，速率显示 0，最后一批数据已显示；
- 点“清空”：表格立即清空，总样本归零，滚动条回到顶部；
- 快速拉伸窗口：数值列跟着变宽，其他列保持可读，行高一致；
- 连续运行和反复操作无崩溃、无 Qt 警告。

## 可选实验（建议至少做两个）

**实验 A：用 QTableWidget 做同样的刷新**

保留同样的数据源，但把视图换成一个 `QTableWidget`：每次 flush 用 `setRowCount()` 和 `setItem()` 重建可见行（或 append 后删最旧行）。运行 30 秒，对比 CPU 占用、滚动流畅度和代码量。想一想：为什么这个方法在数据量增大时更糟？

**实验 B：改批量周期**

把 flush 周期分别改成 16ms、50ms、500ms、1000ms，观察界面观感和 CPU。记录：

- 周期太短，信号和重绘开销增加多少？
- 周期太长，数据是不是“一阵一阵”出现？
- 对表格来说，哪个范围最舒服？

**实验 C：全表 dataChanged**

临时把 `appendSamples` 改成“数据照常追加，但只发一次覆盖全表的 `dataChanged`”，不发送 insert。观察表格是否显示新行、滚动是否正常、`rowCount()` 与视图是否一致。再改回来，理解为什么结构变化必须用 insert/remove。

**实验 D：用户滚动时自动取消跟随**

给滚动条加监听：用户向上滚动时自动取消“自动跟随”；用户滚回底部时自动恢复。注意区分用户操作和程序调用 `scrollToBottom()` 触发的值变化（用 `QSignalBlocker` 或标志位）。

**实验 E：按通道过滤**

用 `QSortFilterProxyModel` 包一层，加一个通道下拉框。注意：

- 视图设置的是代理模型，不是源模型；
- 过滤时行的 row 是代理行，取原始数据要用 `mapToSource()`；
- 实时数据下过滤会不断重置视图，观察体验；考虑“暂停采集后才能过滤”的设计。

**实验 F：自定义 delegate**

写一个 `QStyledItemDelegate`，把“数值”列画成一个小横条（以 0～100 映射宽度），并在 `paint()` 中调用基类绘制文本。对比 `ForegroundRole` 和 delegate 两种做法的复杂度与性能。

## 验收清单

- [ ] 构建成功，0 warning；
- [ ] 工程拆分为数据、模型、界面三层，没有把模型逻辑写进 `MainWindow`；
- [ ] `RingBuffer` 固定容量，运行期不扩容，追加/淘汰 O(1)；
- [ ] 模型正确实现五个虚函数，表格能显示五列；
- [ ] `appendSamples` 只发 remove/insert，不使用 reset；
- [ ] 缓冲满后行数稳定在 5000，旧行被淘汰，表格不跳回顶部；
- [ ] 400 样本/秒运行时界面可交互，滚动、按钮、拉伸窗口不卡死；
- [ ] “开始/停止/清空”行为正确，停止后没有残留定时器继续写数据；
- [ ] 总样本、速率、缓冲占用三个计数正确；
- [ ] 自动跟随勾选/取消行为正确；
- [ ] 警告/异常行颜色正确；
- [ ] 连续运行 2 分钟以上，内存不持续增长，无 Qt 警告；
- [ ] 能解释 `beginRemoveRows` / `beginInsertRows` 的顺序为什么不能颠倒；
- [ ] 能解释为什么生成频率是 100Hz、界面刷新是 10Hz。

## 思考

1. 为什么要有 `m_pending`，而不是在 `generateTick()` 里直接 `appendSamples({sample})`？如果去掉 pending，信号数量会变成多少？
2. 如果把 flush 周期改成 1 秒，界面和数据各有什么变化？总吞吐量变了吗？
3. `appendSamples()` 里为什么先算 `overflow`、先删旧行，再插入新行？如果先插入再删除，`beginRemoveRows` 的行号会怎样？
4. 缓冲满后，为什么用 remove + insert，而不是 reset 后重建？reset 会让哪些状态丢失？
5. 为什么速率统计用 `QElapsedTimer`，而不是“计数 × 10Hz”？系统定时器不精确时，哪种方法更接近真实？
6. 如果把表格换成 `QTableWidget`，在 400 样本/秒下最先出现的性能问题是什么？
7. 用户在实时刷新时选中了一行，下一批数据从头部删除了几行，选中项会怎样？为什么？
8. “清空”时如果正在运行，定时器还在跑。代码里哪些计数器必须一起重置？漏掉会看到什么现象？
9. 如果主线程被一个 5 秒的模态对话框阻塞，pending 会变成什么样？恢复后一次提交多少条？模型能承受吗？内存有没有风险？
10. 单元 07 把生成器移到工作线程后，`appendSamples()` 应该由哪个线程调用？跨线程需要传什么类型？为什么不能直接把模型指针给工作线程？

---

## 参考实现说明

工程内含 `main.cpp`、`telemetrymodel.h/.cpp`、`ringbuffer.h`、`telemetrytypes.h`。参考实现已在 Qt 6.4.2 + GCC 下用 `-Wall -Wextra` 编译通过（0 warning），并用 `QAbstractItemModelTester` 验证过 insert/remove 信号契约。

建议顺序：先自己写完并过验收清单，再打开对照。重点看 `appendSamples()` 里 remove/insert 的顺序，以及 flush 周期与生成周期的分离。