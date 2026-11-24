# Qt6 Learn Lab

Qt 6 Widgets 学习实验，共 12 个，覆盖事件循环、信号槽、Model/View、TCP 分帧、工作线程、自绘图表、SQLite、配置与日志、测试和打包。

每个 `labs/labNN_*` 目录包含任务说明 `README.md` 和参考实现。用 Qt Creator 打开目录下的 `CMakeLists.txt` 构建，或在命令行执行：

```bash
cmake -B build && cmake --build build
```

需要 Qt 6.4 及以上。`lab11_release` 使用 `CMakePresets.json`，`tests/` 下是 Qt Test 用例。

## 实验列表

| 编号 | 主题 | 实验 |
|---|---|---|
| 00 | 环境与第一个 Qt 6 窗口 | Hello 窗口 |
| 01 | 对象模型、事件循环与定时器 | 定时刷新与暂停面板 |
| 02 | 信号槽与跨线程语义 | 跨线程信号 demo |
| 03 | Widgets 布局与主窗口骨架 | 仿制连接页 |
| 04 | Model/View 与高频刷新 | 100Hz 遥测表格 |
| 05 | Qt 核心类型与二进制编解码 | 自定义报文编解码 |
| 06 | Qt Network 与流式解包 | 分帧服务端与客户端 |
| 07 | QThread 与工作线程模型 | 网络 worker |
| 08 | QPainter 自绘趋势图 | 实时曲线 |
| 09 | SQLite 持久化 | 遥测落盘与查询 |
| 10 | 文件、配置、CSV 与日志 | 日志与导出模块 |
| 11 | Qt Test 与 Windows 构建打包 | 可发布 exe |