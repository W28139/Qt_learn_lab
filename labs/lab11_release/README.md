# Lab 11：Qt Test 与 Windows 构建打包

## 需求

1. CMake ≥ 3.21，至少三个 target：
   - `lab11_core`：静态库，纯逻辑，只依赖 `Qt6::Core`；
   - `lab11_release`：GUI 程序，链接 `Qt6::Widgets` 和 `lab11_core`；
   - `tst_sessionstats`、`tst_csvutils`：测试程序，链接 `Qt6::Test` 和 `lab11_core`；
   - 顶层负责 `enable_testing()`、`add_subdirectory(tests)`、install 和部署；测试在 Windows 上为控制台程序。
2. `SessionStats`：按通道维护样本数、增量均值、最小值、最大值、质量计数；`reset()` 清空；`channels()` 升序；`channelStats()` 对不存在的通道返回 false 且不改写输出；空统计 `isEmpty()` 为 true；坏质量样本也计入均值。
3. `csvutils`：`escapeField()` 对逗号、双引号、换行加引号并翻倍内部引号；`toUtf8Line()` 拼接字段、逗号分隔、CRLF 结尾，返回 UTF-8 字节。
4. GUI：标题标明 Lab 11，顶部显示构建类型和 Qt 版本；按钮“生成示例数据”（4 通道 × 200 样本）和“导出摘要 CSV”；表格显示通道、样本数、均值、最小、最大、正常/警告/异常；导出用 UTF-8 BOM、CRLF 和 `QSaveFile`。
5. Qt Test：
   - `tst_sessionstats` 覆盖空统计、单样本、多通道、质量计数、reset，表驱动均值至少 4 组；
   - `tst_csvutils` 表驱动转义至少 7 组，并测试整行字节；
   - 使用 `QTEST_GUILESS_MAIN`，测试不依赖网络、数据库、系统时间和用户目录。
6. CTest：每个测试 `add_test` 注册，`TIMEOUT 30`，`LABELS unit`；`ctest -R`、`ctest -L` 可过滤；故意失败时返回非 0 并显示断言行。
7. `CMakePresets.json`：`base` 使用 Ninja、`build/${presetName}`、`CMAKE_PREFIX_PATH=$env{QT_ROOT}`；提供 windows-debug/release，release 安装前缀为 `dist`；configure/build/test 三条 preset 命令能跑通。
8. 安装部署：`install(TARGETS ...)` + `qt_generate_deploy_app_script`（Windows 上调用 `windeployqt`）；`dist` 至少包含 exe、Qt DLL、`platforms/qwindows.dll` 和编译器运行库；在干净环境能启动。

## 验收

- [ ] `lab11_core` 不依赖 Widgets/Network/Sql，app 和测试共同链接它；
- [ ] `SessionStats` 的统计、排序、reset、边界行为正确；
- [ ] `csvutils` 对逗号/引号/换行/空字段/中文转义正确；
- [ ] 两个测试单独运行全部 PASS，表驱动数据量达到要求；
- [ ] `ctest --output-on-failure` 2/2 通过，`-R`、`-L` 过滤生效；
- [ ] 故意让断言失败时 CTest 返回非 0 并显示失败行；
- [ ] preset 命令链跑通，Qt Creator 能识别；
- [ ] `cmake --install` 生成完整 dist，干净环境能启动并导出摘要；
- [ ] 发布目录不含 Debug DLL、测试 exe、源码和 PDB；
- [ ] 0 warning。

参考实现见本目录。