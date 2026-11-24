# Lab 08：QPainter 实时趋势图

> **配套知识**：[单元 08 - QPainter 自绘趋势图](../../08-QPainter自绘趋势图.md)
> **工程位置**：`D:\Qt_\learn_demo\lab08_trendchart\`（Windows，用 Qt Creator 打开）
> **参考实现**：工程内的 `.cpp/.h` 和 `CMakeLists.txt`。先按本文档自己写，写完再对照。

---

## 目标

把单元 07 送进 GUI 的样本真正画出来：

- 用 `QPainter` + `paintEvent()` 自绘多通道实时曲线，不使用 Qt Charts；
- 横轴是时间滑动窗口，纵轴按可见数据自动缩放；
- 每个通道一个固定容量环形缓冲，窗口右边界跟随最新样本；
- 点数多于像素宽度时按像素列做 min/max 降采样；
- 暂停冻结时间轴但继续接收数据，恢复后跳回最新；
- 用状态栏指标观察重绘频率、单帧耗时和可见点数。

这个 Lab 的重点是**绘制和坐标映射**，数据链路直接复用单元 07。

## 前置

- 单元 04：批量更新、不要在逐样本上刷新界面；
- 单元 07：`TelemetryWorker`、`BoundedBatchQueue`、GUI 定时 `takeAll()` 的完整链路；
- 单元 05/06：协议、CRC、`StreamDecoder` 可用，不要修改线上格式。

## 需求

### 1. 工程

- 工程名 `lab08_trendchart`，GUI 程序，依赖 `Qt6::Core`、`Qt6::Network`、`Qt6::Widgets`；
- 保留控制台输出（worker/绘图日志），CMake 中设置 `WIN32_EXECUTABLE FALSE`；
- C++20，MSVC 下带 `/utf-8`；
- 构建 0 warning；
- 复用单元 05/06/07 的 `protocol.*`、`crc32.*`、`streamdecoder.*`、`telemetrytypes.h`、`boundedqueue.*`、`telemetryworker.*`、`mocktelemetryserver.*`，按需改 mock 的演示数据，不改协议。

### 2. ChartWidget（本 Lab 的核心）

自己写一个继承 `QWidget` 的 `ChartWidget`，不要在 `paintEvent()` 之外画图：

- 提供 `appendSamples(const QList<TelemetrySample> &)`：只追加数据并请求一次 `update()`，不直接绘制；
- 每个通道一个固定容量环形缓冲，容量要覆盖“最大采样率 × 最大窗口”；
- 忽略非有限值（NaN/Inf），不要让一个坏点毁掉坐标映射；
- 横轴可见窗口可配置（建议 5/10/30/60 秒），右边界默认跟最新样本时间，左边界 = 右边界 − 窗口；
- 暂停时冻结右边界，数据仍继续写入；恢复后右边界回到最新；
- 所有绘制都在 `paintEvent()` 内：背景、标题、绘图区、网格、刻度、曲线、图例、暂停徽标、空状态；
- 绘图区四周留出边距，曲线必须裁剪在绘图区内；
- Y 轴刻度用 1/2/5 × 10ⁿ 的 nice step，约 5 个间隔，小数位由步长决定；
- X 轴按时间步长画刻度并对齐到步长整数倍，标签用 `HH:mm:ss`；
- 自动缩放：按窗口内可见数据加 padding，取整到 nice step；窗口内没有数据时保留上一范围；数据全相等时给出最小范围；
- 可切换固定范围（0~100），作为自动缩放的兜底；
- 每个通道一条折线，用一次 `drawPolyline()` 完成；曲线抗锯齿，网格线保持清晰；
- 每个通道画最新点标记；图例显示颜色、通道名、最新值；
- 可见点数超过绘图区像素宽度时，按像素列保留 first/min/max/last 四点再连线，不能简单隔点采样；
- 每次重绘复用屏幕坐标缓冲，禁止在 `paintEvent()` 里反复分配大容器；
- 暴露指标：累计重绘次数、最近一次绘制耗时（ms）、本次可见点数。

### 3. MainWindow

- 复用单元 07 的 `TelemetryWorker` + `BoundedBatchQueue`：worker 仍然在网络工作线程收包、解码、入队；
- GUI 用 40 ms 定时器 `takeAll()`，展开成样本列表后**一次** `chart->appendSamples()`；
- 控件：
  - 开始 / 停止 / 清空数据；
  - 暂停（可勾选，文字在“暂停/继续”间切换）；
  - 时间窗口下拉框（5/10/30/60 秒）；
  - 自动缩放复选框、网格复选框；
- 状态显示：连接状态 + detail、帧/样本/重连数、队列深度/峰值/丢弃、重绘次数/最后耗时/点数/GUI 已消费样本；
- 开始/停止/关窗的生命周期沿用单元 07：`moveToThread`、`started → start()`、`stop → quit → wait`、`finished → deleteLater`；
- 从暂停状态点“开始”时自动恢复，否则新数据会写在冻结窗口右侧看不见；
- 模型和绘图都只能在 GUI 线程更新。

### 4. 数据源

- 默认在主线程创建 `MockTelemetryServer`，监听 `127.0.0.1`，端口 0 由系统分配；
- 每个客户端 50 ms 发一帧（20 Hz），每帧 4 个样本，通道 1..4 各一个；
- 4 个通道用不同基线/相位的正弦波，参考值约 38 / 46 / 54 / 62，幅度 6，加少量抖动；
- 可选：第 N 帧翻转 CRC，验证 worker 的协议错误处理；
- 也允许 `--host/--port` 连接外部服务端（例如 lab07 或 lab06 的服务端）。

### 5. 命令行

参考实现提供：

- 默认：GUI + 内嵌 mock server；
- `--host H --port P`：GUI + 外部服务端；
- `--auto-seconds N`：运行 N 秒后自动关窗；
- `--screenshot PATH`：退出前保存窗口 PNG（未设置 `--auto-seconds` 时默认运行 3 秒后保存）。

`--screenshot` 不是课程重点，但它让无头环境也能验证“确实画出了东西”，建议保留。

## 步骤

1. 建工程，复制单元 05/06/07 的协议、CRC、解包器、队列、worker、mock server；
2. 先写 `ChartWidget` 骨架：填背景、画标题、画空状态，让窗口能弹出；
3. 加每通道环形缓冲和 `appendSamples()`，先只记录最新时间；
4. 加绘图区矩形和坐标映射，先用固定 0~100 范围画网格与坐标轴；
5. 画单通道折线，确认 Y 轴方向和裁剪正确；再扩展到多通道；
6. 实现时间窗口过滤和右边界跟随最新样本；
7. 实现 nice step、Y 轴自动缩放和 X 轴时间刻度；
8. 加图例、最新点标记、暂停冻结、空状态、暂停徽标；
9. 加 min/max 降采样和绘制指标（次数、耗时、可见点数）；
10. 接入 MainWindow：40 ms 取队列、一次追加、控制栏与状态栏；
11. 在 10 秒窗口下运行，确认 4 条曲线稳定滚动；
12. 切到 60 秒并运行足够久，确认点数超过宽度后曲线无异常；
13. 用 `--auto-seconds` + `--screenshot` 跑一次无头验证，再按验收清单实测。

## 运行后应该看到

参考实现运行约 12 秒后的截图（10 秒窗口已铺满，Y 轴按可见数据自动缩放到 20～80）：

![参考实现运行约 4 秒的截图](screenshot.png)

界面上：

- 4 条不同颜色的正弦曲线从右向左滚动，最新点有小圆标记；
- 左上标题显示“实时趋势 · 最近 N 秒”；
- 右上图例显示通道名和最新值；
- 左侧 Y 轴刻度取整到 20/40/60/80 这类值，底部时间刻度每 2 秒一个（10 秒窗口）；
- 状态栏显示“队列 0/64 | 峰值 … | 丢弃 0”和“绘制 … 次 | 最后 … ms | 点 … | 已消费 …”；
- 点“暂停”：曲线冻结，右上出现“已暂停”徽标，后台计数继续增长；点“继续”直接跳到最新；
- 切换 5/30/60 秒窗口，横轴范围和刻度立即变化；
- 取消“自动缩放”后 Y 轴固定 0~100；关闭“网格”后只剩坐标轴和曲线；
- 拖动窗口、点按钮时界面保持响应。

控制台输出（数值随运行时长变化）：

```text
[mock] mock server listening on 127.0.0.1:35749
[worker] worker started, connecting to 127.0.0.1:35749
[worker] connected, peer=127.0.0.1:35749
[mock] client connected: 127.0.0.1:58740
[gui] screenshot saved: C:/tmp/lab08_chart.png
[gui] shutdown complete: gui_consumed=312 queue_dropped=0 queue_peak=1 paints=80
[worker] worker stopped by request
[mock] client disconnected, 0 remaining
```

`gui_consumed`、`paints`、队列峰值不要求与参考实现一致；`queue_dropped` 长期不为 0 说明 GUI 消费太慢。

## 可选实验（建议至少做两个）

**实验 A：窗口长度**

分别用 5 / 10 / 30 / 60 秒运行 20 秒，观察曲线密度、X 轴刻度数量、状态栏点数。思考：窗口越长，绘制成本由什么决定？

**实验 B：暂停语义**

暂停 10 秒再恢复，观察曲线是“平滑接上”还是“跳到最新”。再改成“暂停时直接停止取队列”，观察队列深度和丢弃数，说明两种“暂停”的区别。

**实验 C：自动 vs 固定**

记录 30 秒内自动缩放给出的 Y 范围变化次数；切到固定 0~100 后曲线是否被压扁。思考：什么场景应该固定量程？

**实验 D：降采样**

切到 60 秒并运行到点数超过控件宽度，观察状态栏“点”数与控件宽度的关系；临时把 min/max 降采样改成“每隔 N 个取一个”，构造窄尖峰，比较曲线是否丢峰。

**实验 E：折线 vs 逐点**

临时把 `drawPolyline()` 改成逐点 `drawLine()`，用 60 秒窗口观察单帧耗时变化。恢复后说明差异来源。

**实验 F：缓存网格**

新增一个 `QPixmap` 成员：范围、尺寸、网格开关不变时直接贴图，变化时重画缓存。测量单帧耗时，讨论什么情况下值得缓存。

**实验 G：鼠标十字线**

重写 `mouseMoveEvent()`，把鼠标 x 反算成时间、在最近样本上显示值。注意鼠标坐标是控件坐标，需要反解窗口和 Y 轴映射。

**实验 H：深色配色**

用 `QPalette` 或固定颜色做深色版本，检查网格、坐标轴文字、曲线、暂停徽标在深色下的可读性；确认颜色不是唯一区分手段（图例有名称）。

## 验收清单

- [ ] 构建成功，0 warning；
- [ ] 默认使用内嵌 mock server，不需要第二个进程就能看到曲线；
- [ ] 4 条曲线颜色不同、持续从右向左滚动，最新点在右边缘；
- [ ] 所有绘制都在 `paintEvent()` 中，worker 线程不碰任何 GUI 对象；
- [ ] 曲线被裁剪在绘图区内，不覆盖坐标轴文字和图例；
- [ ] Y 轴刻度是整齐的 1/2/5 × 10ⁿ 倍数，小数位与步长匹配；
- [ ] X 轴时间刻度对齐到整秒/整分步长，标签不重叠；
- [ ] 自动缩放能包住可见数据，窗口内无数据时 Y 轴不闪跳；
- [ ] 固定范围 0~100 可切换，曲线仍正确裁剪；
- [ ] 5/10/30/60 秒窗口切换后，横轴范围和刻度立即更新；
- [ ] 暂停冻结右边界、显示徽标，后台仍接收；继续后跳到最新；
- [ ] 点数超过绘图区宽度时使用 min/max 降采样，尖峰不被抹掉；
- [ ] `paintEvent()` 中没有大容器分配/字符串拼接循环；
- [ ] 状态栏能显示重绘次数、最后耗时、可见点数、已消费样本数；
- [ ] 队列丢弃数长期为 0（默认 20 Hz 数据源下）；
- [ ] 连续启动/停止至少 5 次，无崩溃、无挂起、无 QThread 警告；
- [ ] 关闭窗口时先停 worker 再退出，控制台无 “Destroyed while thread is still running”；
- [ ] 能解释“为什么用数据时间做右边界而不是本地当前时间”；
- [ ] 能解释“为什么每个样本调用一次 update() 是坏做法”。

## 思考

1. `paintEvent()` 由谁触发？`update()` 和 `repaint()` 分别适合什么场景？
2. 为什么 `ChartWidget::appendSamples()` 不直接画图，而是只改数据 + `update()`？
3. 时间窗口为什么用服务器时间戳？如果改用本地 `QDateTime::currentMSecsSinceEpoch()`，断线和暂停时会发生什么？
4. 环形缓冲容量应该怎么估？容量不足时，画面和状态栏分别会有什么表现？
5. 为什么不能用 `QList::removeFirst()` 实现高频滑动窗口？Qt 6 的 `QList` 内部是什么结构？
6. Y 轴映射公式里哪一步实现“向上增大”？如果忘了翻转，曲线会是什么样？
7. 数据全相等、窗口内无数据、控件被压缩到很小，三种情况分别怎么处理？
8. nice step 是怎么算出来的？为什么刻度步长不直接取 `range / 5`？
9. 自动缩放为什么会抖动？nice step、滞回、固定量程分别解决哪部分问题？
10. 为什么画曲线前要 `setClipRect(plot)`？裁剪能省掉哪些边界判断？
11. min/max 降采样为什么每列要保留 first/min/max/last 四个点？只保留 min 和 max 会怎样？
12. 如果单帧绘制耗时 25 ms，你会按什么顺序定位？哪些指标能证明优化有效？
13. 暂停时数据继续进环形缓冲，长时间暂停后恢复会丢掉中间数据吗？为什么？
14. 如果要把这张图导出成 PNG，`grab()` 得到的是逻辑像素还是物理像素？高 DPI 下要注意什么？
15. 如果要求 1000 Hz 采样、60 秒窗口，每个通道要保留多少点？是否应该改变降采样策略或数据结构？
16. 为什么 `QPixmap` 不能在 worker 线程画？`QImage` 的特殊性在哪里？

---

## 模块关系

```text
main.cpp
  ├── 默认 → 主线程创建 MockTelemetryServer（20 Hz，4 通道）
  ├── --host/--port → 连接外部服务端
  ├── --auto-seconds / --screenshot → 无头验证与截图
  └── MainWindow
        ├── 控制栏：开始/停止/清空/暂停/窗口/自动缩放/网格
        ├── ChartWidget（核心）
        │     ├── 每通道 RingBuffer<SamplePoint>（容量 20000）
        │     ├── 时间窗口过滤 + 坐标映射
        │     ├── nice ticks + 自动缩放
        │     ├── 网格/裁剪/折线/图例/暂停徽标
        │     └── min/max 降采样 + 绘制指标
        ├── GUI 定时器 40 ms
        │     └── queue.takeAll() → 展开样本 → chart->appendSamples()
        └── QThread
              └── TelemetryWorker（moveToThread）
                    ├── QTcpSocket / StreamDecoder / 重连定时器
                    └── queue.push(TelemetryBatch)

共享：protocol.* / crc32.* / streamdecoder.* / telemetrytypes.h（来自 05、06）
      boundedqueue.* / telemetryworker.* / mocktelemetryserver.*（来自 07）
      ringbuffer.h（来自 04，cap 由 ChartWidget 决定）
```

关键点：

- 线程边界仍然只有 `queue.push()` 和 `queue.takeAll()`；
- 绘制边界只有 `appendSamples()` 和 `paintEvent()`：前者改数据，后者消费数据；
- ChartWidget 不持有 socket、不持有 worker；MainWindow 不直接画像素；
- 数据时间决定横轴，本地时间只用于统计性能。

## 参考实现说明

工程内含 `main.cpp`、`mainwindow.h/.cpp`、`chartwidget.h/.cpp`、`boundedqueue.*`、`telemetryworker.*`、`mocktelemetryserver.*`、`ringbuffer.h`、`streamdecoder.*`、`protocol.*`、`crc32.*`、`telemetrytypes.h`。

参考实现已在 Qt 6.4.2 + GCC（`-Wall -Wextra`）下构建通过（0 warning），并用 offscreen 平台验证：

- 内嵌 mock server 正常供数，worker 连上并解码；
- 10 秒窗口运行 12 秒：GUI 消费 924 个样本，队列丢弃 0、峰值 1；重绘约 230 次，单帧约 5 ms（offscreen 软件渲染）；
- 60 秒窗口运行 55 秒：窗口内约 4400 个点，超过绘图区宽度，min/max 降采样路径生效；曲线、刻度、图例正常，单帧约 20 ms，仍在 25 Hz 的 40 ms 预算内；
- 关窗时 worker 在超时内停止，退出码 0，mock 连接清理到 0。

不同机器/平台的绘制耗时会明显不同；验收只要求“界面流畅、单帧在预算内”，不要求和参考数字一致。

建议顺序：先自己写完并过验收清单，再打开对照。重点看 `ChartWidget::appendSamples()`、`paintEvent()`、坐标映射、`computeYRange()`、`drawSeries()` 的降采样分支，以及 `MainWindow::drainQueue()` 如何做到“每批一次重绘”。

## 完成后

按 [README](../../README.md) 的规则回复“**单元 8 完成**”，并附上：

```text
单元 8 完成
- 构建是否 0 warning：
- 能否同时看到 4 条实时曲线：
- 暂停/继续是否冻结并跳回最新：
- 5/10/30/60 秒窗口切换是否正常：
- 自动缩放/固定范围是否都正常：
- 状态栏单帧绘制耗时：
- 60 秒窗口下是否出现降采样，曲线是否无明显异常：
- 自测题有没有不会的：
```