# Lab 03：客户端主窗口骨架

## 需求

1. 标题 `Lab 03 - Client Skeleton`，初始 `900 x 600`，最小 `720 x 480`。
2. `QMainWindow`：
   - 文件菜单：连接（`F5`）、断开（`Shift+F5`）、分隔线、退出（`Ctrl+Q`）；
   - 帮助菜单：关于 Lab 03；
   - 工具栏含连接/断开，只显示文字、不可拖动；
   - 状态栏：启动时临时显示“就绪”，右侧永久显示连接状态和端点。
3. 中央 `QTabWidget`：连接页 `ConnectionPage`、实时数据页（居中占位标签）。
4. `ConnectionPage`：
   - 左侧“服务器”组（`QFormLayout`）：主机默认 `127.0.0.1`，带清除按钮；端口 `1～65535`，默认 9000；协议 TCP/UDP；会话名称默认 `default`，最长 32；自动重连复选框；
   - “操作”组：自动换行的提示标签 + 连接/断开按钮；
   - 右侧“会话日志”：只读 `QPlainTextEdit`，最多 500 行；
   - 页面外边距和间距 12，日志区占满剩余空间。
5. 用 `QTimer` 模拟连接：
   - 连接前校验主机和会话名非空，失败用 `QMessageBox::warning` 提示并聚焦对应输入框，状态不变；
   - 通过后进入 Connecting，禁用参数输入和连接按钮，启用断开，800ms 后变 Connected；
   - 断开可取消 Connecting 或断开已连接，最终回到 Disconnected。
6. 状态变化统一走 `setState()` + `updateUi()`，并发 `stateChanged` 让主窗口同步 action 和状态栏。
7. “关于”用 `QMessageBox::about` 显示应用名、版本和 `qVersion()`。
8. 非 Disconnected 状态关闭窗口时用 `QMessageBox::question` 确认，选“否”不关闭。

## 可选实验

- 用 `QGridLayout` 替换 `QFormLayout`，对比窗口拉宽时的对齐；
- 用 `QSplitter` 替换页面左右布局；
- 增加 Error 状态，只改 `setState()` 和 `updateUi()`。

## 验收

- [ ] 菜单、工具栏、快捷键共用同一组 `QAction`，可用状态同步；
- [ ] 参数为空时提示且状态不变，焦点回到出错字段；
- [ ] 连接中 → 已连接约 800ms；连接中可取消，取消后不会又变成已连接；
- [ ] 状态栏临时消息与右侧永久标签正确更新；
- [ ] 缩放窗口时日志区吃满多余空间，表单保持对齐；
- [ ] 关于对话框正常；非断开状态关窗有确认；
- [ ] 反复连接/断开/切标签/拉伸无崩溃、无 Qt 警告，0 warning。

参考实现见本目录。