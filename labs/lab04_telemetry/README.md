# Lab 04：100Hz 遥测表格

## 需求

1. 工程 `lab04_telemetry`，仅依赖 `Qt6::Widgets`，C++20。
2. 数据结构：
   - `TelemetrySample`：`timestampMs`、`seq`、`channel`、`value`、`quality`（Good/Warning/Bad）；
   - 模板 `RingBuffer<T>`：固定容量，提供 `pushBack/dropFront/at/size/capacity/clear`，`at(0)` 最旧，追加和淘汰都是 O(1)，运行期不扩容。
3. `TelemetryModel : QAbstractTableModel`：
   - 五列：序号/时间/通道/数值/状态，只读；
   - 时间 `HH:mm:ss.zzz`，通道 `CH1..CH4`，数值 3 位小数，序号/时间/数值右对齐；
   - Warning 橙色，Bad 红色；
   - `appendSamples()` 超容量时先 `beginRemoveRows` → `dropFront` → `endRemoveRows`，再 `beginInsertRows` → `pushBack` → `endInsertRows`，禁止 reset；
   - `clear()` 用 reset。
4. 模拟数据：4 通道各 100Hz，10ms 生成 4 条同时间戳样本；序号全局递增；数值 `50 + 12 * sin(...) + noise`；用固定规则产生少量 Warning/Bad。
5. 批量刷新：数据先放进 `m_pending`，100ms 定时器 flush 时调一次 `appendSamples()`，不逐条刷新。
6. 界面：
   - 标题 `Lab 04 - Telemetry Model/View`，`1000 x 640`；
   - 顶部：开始/停止、清空、自动跟随最新（默认勾选），右侧显示实际速率和缓冲占用；
   - 中央 `QTableView`：只读、行选择、隔行变色、隐藏垂直表头、等宽字体、数值列拉伸；
   - 状态栏显示总样本数；缓冲容量 5000。
7. 行为：停止时提交最后一批；清空重置所有计数；取消自动跟随后不抢滚动条；运行 15 秒后缓冲稳定在 5000 行。

## 可选实验

- 用 `QTableWidget` 重建可见行，对比 CPU、流畅度和代码量；
- flush 周期改成 16/50/500/1000ms，观察观感和开销；
- 用 `QSortFilterProxyModel` 加通道过滤。

## 验收

- [ ] `RingBuffer` 固定容量、O(1)；模型逻辑没有写进 `MainWindow`；
- [ ] `appendSamples` 只发 remove/insert，不使用 reset；
- [ ] 400 样本/秒时界面可交互，滚动、按钮、拉伸不卡死；
- [ ] 缓冲满后行数稳定在 5000，旧行被淘汰，表格不跳回顶部；
- [ ] 开始/停止/清空、三项计数、自动跟随、警告/异常颜色都正确；
- [ ] 连续运行无明显内存增长，无 Qt 警告，0 warning。

参考实现见本目录。