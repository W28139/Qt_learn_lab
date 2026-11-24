# Lab 11：Qt Test 与 Windows 构建打包

> **配套知识**：[单元 11 - Qt Test 与 Windows 构建打包](../../11-QtTest与Windows构建打包.md)
> **工程位置**：`D:\Qt_\learn_demo\lab11_release\`（Windows，用 Qt Creator 打开）
> **参考实现**：工程内的 `.cpp/.h`、`CMakeLists.txt`、`CMakePresets.json`。先按本文档自己写，写完再对照。

---

## 目标

把“能运行”变成“可证明正确、可交付”：

- 把纯逻辑拆成核心静态库，app 和测试共同链接；
- 用 Qt Test 写普通测试和表驱动测试，覆盖空输入、边界和多组数据；
- 用 CTest 注册、过滤、并行运行测试，并作为发布前的门槛；
- 用 `CMakePresets.json` 固定 Debug/Release 的 generator、构建目录和 Qt 路径；
- 用 `install` + `qt_generate_deploy_app_script`（Windows 上调用 `windeployqt`）生成发布目录；
- 在干净环境验证发布包，能解释缺 DLL、缺平台插件、Debug/Release 混用等典型故障。

本 Lab 的核心是**可测试性、测试组织、构建配置、发布边界**；不新增网络/数据库功能。

## 前置

- 单元 00：Qt Creator、CMake、Kit；
- 单元 05：`QString`/`QByteArray`、CSV 转义的基本概念（单元 10 也讲过）；
- 单元 10：文件写入、`QSaveFile`、安装目录与用户数据目录的区别；
- Windows 上已安装 Qt 6.8.3（MinGW）和 Qt Creator。

## 需求

### 1. 工程结构

- 工程名 `lab11_release`，C++20，CMake ≥ 3.21；
- 至少三个 target：
  - `lab11_core`：静态库，纯逻辑，只依赖 `Qt6::Core`；
  - `lab11_release`：GUI 可执行文件，链接 `Qt6::Core`、`Qt6::Widgets`、`lab11_core`；
  - `tst_sessionstats`、`tst_csvutils`：测试可执行文件，链接 `Qt6::Test`、`lab11_core`；
- 顶层 `CMakeLists.txt` 负责核心库、app、`enable_testing()`、`add_subdirectory(tests)`、安装与部署；
- `tests/CMakeLists.txt` 只负责测试目标和 `add_test`；
- 构建 **0 warning**；MSVC 带 `/utf-8`，GCC/Clang 带 `-Wall -Wextra`；
- Windows 上测试目标必须是控制台程序（`WIN32_EXECUTABLE FALSE`）。

### 2. 核心逻辑（SessionStats）

实现一个不依赖界面/数据库/网络的样本统计类：

- 输入：`TelemetrySample`（通道号、数值、质量枚举）；
- 按通道维护：
  - 样本数；
  - 均值（增量更新，不保存全部样本）；
  - 最小值、最大值；
  - 正常/警告/异常计数；
- `reset()` 清空所有统计；
- `channels()` 按通道号升序返回出现过的通道；
- `channelStats(channel, out)` 在通道不存在时返回 false，且不改写 out；
- 空统计时 `totalCount()==0`、`isEmpty()==true`；
- 均值公式：`mean += (value - mean) / count`，注意新样本加入时的分母；
- 质量计数与数值统计相互独立；文档里明确“坏质量样本是否计入均值”（参考实现计入）。

### 3. CSV 工具

实现可测试的纯函数：

- `escapeField(value)`：字段含逗号、双引号、`\n` 或 `\r` 时整体加双引号，内部双引号翻倍；其他情况原样返回；
- `toUtf8Line(fields)`：逐字段转义、用逗号连接、追加 CRLF，返回 UTF-8 字节；
- 空字符串、中文、普通文本、含标点都要有确定行为；
- 供 app 导出“每通道摘要”使用；
- 不依赖文件、不依赖界面。

### 4. GUI 程序

一个最小但完整的窗口：

- 标题标明 Lab 11；
- 两个按钮：“生成示例数据”“导出摘要 CSV…”；
- 一张表：通道、样本数、均值、最小、最大、正常、警告、异常；
- 顶部显示构建类型（Debug/Release）和 Qt 版本；
- 生成示例数据：固定 4 通道 × 200 样本，数值用确定性公式（正弦），质量按固定规则产生警告/异常，方便对比；
- 生成后更新表格和“总样本”标签；
- “导出摘要 CSV…”：
  - 没有数据时提示先生成；
  - 文件对话框选路径；
  - 写 UTF-8 BOM + 表头 + 每通道一行，CRLF；
  - 用 `QSaveFile`；失败时弹窗并保留旧文件；
  - 成功后状态栏显示导出路径；
- 关闭窗口正常退出。

### 5. Qt Test

至少两个测试可执行文件：

**tst_sessionstats**

- 空统计；
- 单样本；
- 多通道：通道排序、每通道计数/均值/最小/最大；
- 质量计数；
- `reset()`；
- 表驱动测试：至少 4 组数据验证均值；
- 至少覆盖：空输入、单元素、负数、小数、不均匀数据；
- 使用 `QCOMPARE`/`QVERIFY`/`QFETCH`，浮点用容差比较；
- 使用 `QTEST_GUILESS_MAIN`，不创建窗口。

**tst_csvutils**

- 表驱动测试转义：普通文本、空字段、逗号、双引号、换行、CRLF、中文；
- 测试整行拼接的字节结果（包括 CRLF）；
- 至少 7 组转义数据。

测试要求：

- 不依赖网络、真实数据库、系统时间、用户目录；
- 每个测试独立准备数据；
- 断言信息能定位失败；
- 测试函数名描述行为；
- Windows 上能通过 Qt Creator 或命令行运行，输出可见。

### 6. CTest

- 顶层 `enable_testing()`；
- 每个测试目标 `add_test` 注册；
- 设置 `TIMEOUT 30` 和 `LABELS "unit"`；
- `ctest --test-dir build/<preset> --output-on-failure` 能跑通；
- `ctest -R sessionstats`、`ctest -L unit` 能按名称/标签过滤；
- 至少验证一次“故意让断言失败，CTest 返回非 0，`--output-on-failure` 显示失败行”，然后改回。

### 7. CMakePresets

提供 `CMakePresets.json`（version 3）：

- 公共 `base`：generator `Ninja`、`binaryDir = build/${presetName}`、`CMAKE_PREFIX_PATH = $env{QT_ROOT}`；
- `windows-debug` / `windows-release`：
  - `QT_ROOT` 默认指向 `D:/Qt_/6.8.3/mingw_64`（按自己的安装改）；
  - 分别设置 `CMAKE_BUILD_TYPE`；
  - release 设置 `CMAKE_INSTALL_PREFIX = ${sourceDir}/dist`；
- `linux-debug` / `linux-release`（可选，但建议保留）：
  - `QT_ROOT=/usr`；
- 对应的 `buildPresets`、`testPresets`；
- 能用以下命令链完成配置、构建、测试：

```text
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

- Qt Creator 中选择 preset 后能正确使用对应 Kit/构建目录；
- 命令行找不到 Ninja 时，把 Qt Tools 的 Ninja 目录加入 PATH，或把 generator 改成 MinGW Makefiles；Qt Creator 通常已自带 Ninja；
- 不把 `build/`、`dist/` 提交到源码目录（本 Lab 只保留源代码）。

### 8. 安装与部署

- `install(TARGETS lab11_release RUNTIME DESTINATION . BUNDLE DESTINATION .)`；
- 使用 `qt_generate_deploy_app_script`：
  - Qt 6.4 用 `FILENAME_VARIABLE`，Qt 6.5+ 用 `OUTPUT_SCRIPT`；
  - 加 `NO_UNSUPPORTED_PLATFORM_ERROR`，让 Linux 等平台跳过；
  - `install(SCRIPT ${deploy_script})`；
- Windows 上执行：

```text
cmake --install build/windows-release --prefix dist
```

- `dist` 中至少有：
  - `lab11_release.exe`；
  - `Qt6Core.dll`、`Qt6Gui.dll`、`Qt6Widgets.dll`；
  - `platforms/qwindows.dll`；
  - MinGW 运行库（`libgcc_s_seh-1.dll`、`libstdc++-6.dll`、`libwinpthread-1.dll`）或由 MSVC Redistributable 提供；
- 解释 `windeployqt` 不负责的部分（MSVC 运行库、OpenSSL 等）；
- 在干净环境（无 Qt/PATH 干扰）双击 exe，确认能启动；
- 发布前清理：不带 Debug DLL、不带测试 exe、不带 `build/`、源码和 PDB。

## 步骤

1. 建目录：`src/`（核心 + app）、`tests/`；把 `TelemetrySample`/`Quality` 头文件放进 `src/`；
2. 写 `SessionStats`：先实现空统计和单样本，再补多通道、质量计数、reset；
3. 写 `csvutils`：先写转义规则，再写整行拼接；
4. 建 CMake：核心静态库、GUI app、`enable_testing()`、`add_subdirectory(tests)`；
5. 写 `tst_sessionstats`：先用 `QTEST_GUILESS_MAIN` 跑通空统计，再逐个加测试；
6. 写 `_data()` 表驱动均值测试；
7. 写 `tst_csvutils`：表驱动覆盖逗号/引号/换行/中文；
8. 在 `tests/CMakeLists.txt` 注册 `add_test`，运行 `ctest`；
9. 加 `CMakePresets.json`，用 preset 完成配置/构建/测试；
10. 写 GUI：生成数据、填表、导出摘要；
11. 加 `install` 和部署脚本，执行 `cmake --install`，检查 `dist`；
12. 在干净环境运行发布 exe，过验收清单。

## 运行后应该看到

**测试输出**（Linux 参考实现，Windows 数字相同）：

```text
Test project .../build/linux-release
    Start 1: sessionstats
1/2 Test #1: sessionstats .....................   Passed    0.01 sec
    Start 2: csvutils
2/2 Test #2: csvutils .........................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 2

Label Time Summary:
unit    =   0.03 sec*proc (2 tests)
```

QTest 单个可执行文件内部会显示更多行，例如：

```text
PASS   : SessionStatsTest::emptyStats()
PASS   : SessionStatsTest::singleSample()
PASS   : SessionStatsTest::perChannel()
PASS   : SessionStatsTest::qualityCounts()
PASS   : SessionStatsTest::resetClears()
PASS   : SessionStatsTest::mean(1,2,3)
PASS   : SessionStatsTest::mean(negative)
PASS   : SessionStatsTest::mean(decimal)
PASS   : SessionStatsTest::mean(uneven)
Totals: 9 passed, 0 failed, 0 skipped
```

**程序界面**：

- 标题 “Lab 11 - Qt Test 与发布打包”；
- 顶部显示“构建：Release | Qt 6.8.3”；
- 点“生成示例数据”后，表格出现 4 行（CH 1～CH 4），每行 200 个样本，均值、最小值、最大值、质量计数合理；
- “总样本：800”；
- 点“导出摘要 CSV…”保存后，状态栏显示路径；
- 用 Excel 或文本编辑器打开 CSV：

```text
﻿通道,样本数,均值,最小,最大,正常,警告,异常
CH 1,200,62.473,56.986,66.986,193,5,2
CH 2,200,72.310,66.900,76.900,193,5,2
...
```

（具体数值随公式略有差异；行数固定为 1 表头 + 4 通道。）

**安装目录**（Windows，参考）：

```text
dist/
  lab11_release.exe
  Qt6Core.dll
  Qt6Gui.dll
  Qt6Widgets.dll
  platforms/qwindows.dll
  styles/...
  imageformats/...
  libgcc_s_seh-1.dll
  libstdc++-6.dll
  libwinpthread-1.dll
```

Linux 上安装目录只有可执行文件（系统已有 Qt），`NO_UNSUPPORTED_PLATFORM_ERROR` 会跳过 Windows 部署逻辑。

## 可选实验（建议至少做两个）

**实验 A：让测试失败**

故意把某个 `QCOMPARE` 的期望值改错，运行 `ctest --output-on-failure`，观察失败行定位、返回码；再运行 `ctest -R sessionstats -V` 看完整输出。恢复后确认全绿。

**实验 B：表驱动扩展**

给均值测试增加 `0,0,0`、`1e9,1e9,1e9`、`-1,1,0` 等边界；思考浮点比较容差怎么选。

**实验 C：测试临时文件**

给 `csvutils` 增加一个“写入临时文件再读回”的测试，用 `QTemporaryDir` 管理目录，验证 CRLF 和 BOM 字节。确保测试不依赖当前工作目录。

**实验 D：GUI 测试**

用 `QTEST_MAIN` 创建一个最小 GUI 测试，用 `QTest::mouseClick` 点“生成示例数据”，再用 `QSignalSpy` 或读取表格行数验证结果。对比它和纯逻辑测试的速度与稳定性。

**实验 E：preset 对照**

分别用 `windows-debug` 和 `windows-release` 构建，比较生成目录、DLL 名称（`Qt6Cored.dll` vs `Qt6Core.dll`）和 exe 大小。确认 Debug 构建不能作为发布包。

**实验 F：缺插件实验**

把发布目录里的 `platforms/` 临时改名，双击 exe，记录报错；恢复后再运行。思考为什么开发机上不容易复现。

**实验 G：windeployqt 对比**

手工执行一次 `windeployqt --release --verbose 2`，和 `cmake --install` 生成的目录对比；记录哪些文件是 Qt DLL、哪些是插件、哪些是编译器运行库。

**实验 H：干净环境**

把 `dist` 复制到没有安装 Qt 的虚拟机/另一台电脑，或临时从 PATH 移除 Qt/MinGW 路径，测试启动和导出功能。

## 验收清单

- [ ] 构建成功，0 warning；
- [ ] 核心库不依赖 Widgets/Network/Sql，app 和测试都链接它；
- [ ] `SessionStats` 空统计、单样本、多通道、质量计数、reset 行为正确；
- [ ] 均值使用增量公式，结果与朴素求和一致（在容差内）；
- [ ] `channels()` 升序；不存在的通道查询返回 false 且不改写输出；
- [ ] `csvutils` 对逗号/引号/换行/CRLF/空字段/中文转义正确；
- [ ] 两个测试可执行文件都能单独运行并显示 PASS；
- [ ] `tst_sessionstats` 含表驱动均值测试，至少 4 组数据；
- [ ] `tst_csvutils` 含表驱动转义测试，至少 7 组数据；
- [ ] 测试不依赖网络、真实数据库、系统时间、用户目录；
- [ ] `ctest --output-on-failure` 2/2 通过；
- [ ] `ctest -R`、`ctest -L unit` 过滤生效；
- [ ] 故意失败时 CTest 返回非 0 并显示失败断言；
- [ ] `CMakePresets.json` 的 configure/build/test preset 能完成一条命令链；
- [ ] Qt Creator 能识别 preset 并用正确 Qt 路径构建；
- [ ] `build/` 不提交，`dist/` 不混入源码；
- [ ] `install` 生成可执行文件和部署内容；
- [ ] Windows `dist` 含 `platforms/qwindows.dll` 和 MinGW 运行库；
- [ ] 干净环境双击 exe 可启动，生成数据和导出摘要正常；
- [ ] 发布目录不含 Debug DLL、测试 exe、源码、PDB；
- [ ] 能解释缺 Qt DLL、缺平台插件、Debug/Release 混用三类故障；
- [ ] 能写出发布前检查清单。

## 思考

1. 为什么把 `SessionStats` 放进静态库，而不是直接写在 `MainWindow` 里？这给测试带来什么变化？
2. `QTEST_MAIN`、`QTEST_GUILESS_MAIN`、`QTEST_APPLESS_MAIN` 的差别是什么？本 Lab 为什么选 GUILESS？
3. 表驱动测试相比复制多个测试函数，优势在哪里？失败时怎么定位到具体数据行？
4. 均值用增量公式和“保存所有样本再求和”相比，分别有什么取舍？测试如何验证两者一致？
5. 为什么测试里不能 `sleep(1000)` 等结果？如果必须等异步信号，有什么更好的办法？
6. `QCOMPARE` 比较 double 时有什么问题？容差应该怎么选？
7. `initTestCase`、`init`、`cleanup`、`cleanupTestCase` 的先后顺序是什么？哪些属于“每个测试”，哪些属于“整个类”？
8. CTest 和 QTest 的分工是什么？如果只运行一个测试可执行文件，CTest 还有价值吗？
9. 为什么 `add_test` 注册后还要设置 `TIMEOUT` 和 `LABELS`？
10. CMakePresets 的 `base` + `inherits` 解决了什么重复？为什么 `binaryDir` 要放在源码目录外？
11. `CMAKE_PREFIX_PATH` 指向 Qt 的 `mingw_64` 而不是 `D:/Qt_`，原因是什么？
12. 单配置生成器和多配置生成器在 build/test/install 命令上有什么差别？
13. 为什么 Debug 构建不能发布？混用 Debug/Release Qt DLL 会怎样？
14. `windeployqt` 复制哪些东西？MSVC 运行库和 OpenSSL 为什么可能不在里面？
15. 缺少 `platforms/qwindows.dll` 的报错是什么？为什么开发机上不容易发现？
16. 为什么要在“没有安装 Qt”的干净环境验证发布包？PATH 会掩盖哪些问题？
17. 发布目录里为什么不能有 `build/`、测试 exe 和 PDB？它们分别带来什么问题？
18. 如果用户报告“发布版打开就退出”，你会按什么顺序排查？每一层分别看什么？
19. 如果要把前 10 个单元的完整客户端发布出去，需要额外部署哪些插件（Sql/Network/TLS/图像格式）？哪些可以按需裁掉？
20. 你会给这个项目加哪些 CI 步骤？测试、安装、干净环境启动分别放在哪一步？

---

## 模块关系

```text
CMakeLists.txt
  ├─ lab11_core（STATIC，只链接 Qt6::Core）
  │    ├─ sessionstats.h/.cpp   SessionStats / ChannelStats
  │    ├─ csvutils.h/.cpp       escapeField / toUtf8Line
  │    └─ telemetrytypes.h      TelemetrySample / Quality
  ├─ lab11_release（GUI EXE）
  │    ├─ main.cpp              应用标识、版本
  │    └─ mainwindow.h/.cpp     生成数据、统计表、导出摘要
  ├─ tests/CMakeLists.txt
  │    ├─ tst_sessionstats      QTEST_GUILESS_MAIN + 表驱动均值
  │    └─ tst_csvutils          QTEST_GUILESS_MAIN + 表驱动转义
  │         └─ add_test → CTest
  ├─ install(TARGETS) → dist/
  └─ qt_generate_deploy_app_script → windeployqt（Windows）

CMakePresets.json
  ├─ base（Ninja、build/<preset>、$env{QT_ROOT}）
  ├─ windows-debug / windows-release
  └─ linux-debug / linux-release
```

关键点：

- core 是唯一被 app 和测试共享的部分；
- app 不链接 `Qt6::Test`，测试不链接 `Qt6::Widgets`；
- CTest 只认识“测试可执行文件”，不认识内部测试函数；
- preset 固定构建方式，install/deploy 固定发布内容；
- 发布包和开发目录是两种东西，验证必须在干净环境做。

## 参考实现说明

工程内含：

- `src/sessionstats.h/.cpp`：增量均值、min/max、质量计数、通道排序；
- `src/csvutils.h/.cpp`：RFC 4180 转义和整行 UTF-8 拼接；
- `src/mainwindow.h/.cpp`：示例数据、统计表、摘要 CSV 导出；
- `tests/tst_sessionstats.cpp`：空统计、单样本、多通道、质量计数、reset、4 组表驱动均值；
- `tests/tst_csvutils.cpp`：7 组表驱动转义 + 整行字节测试；
- `CMakeLists.txt`：核心库、app、CTest、install、Qt 6.4/6.5+ 部署脚本兼容；
- `CMakePresets.json`：Windows/Linux 的 Debug/Release configure/build/test preset；
- `src/telemetrytypes.h`：与单元 05/10 一致的样本与质量枚举（本 Lab 不做跨线程传值，省略元类型声明）。

参考实现已在 Linux Qt 6.4.2 + GCC 13（`-Wall -Wextra`）下：clean build 0 warning，`ctest` 2/2 通过；`cmake --install` 可生成安装目录（Linux 上跳过 Windows 部署逻辑）。Windows 侧请用 Qt Creator 选择 `windows-release` preset 构建、运行测试，再执行 install 并在干净环境验证 `dist`。Qt 6.4 的部署宏用 `FILENAME_VARIABLE`，Qt 6.8 用 `OUTPUT_SCRIPT`，工程已按版本判断。

建议顺序：先自己写完并过验收清单，再打开对照。重点看：

- `SessionStats::addSample()` 的增量均值和计数顺序；
- `tst_sessionstats.cpp` 的 `_data()`/`QFETCH` 写法；
- `tests/CMakeLists.txt` 的 `add_test`、`TIMEOUT`、`LABELS`；
- `CMakePresets.json` 的 `base` 继承和 `QT_ROOT`；
- 顶层 `install` 与 `qt_generate_deploy_app_script` 的版本兼容；
- `MainWindow::exportSummary()` 的 `QSaveFile` 错误处理。

## 完成后

按 [README](../../README.md) 的规则回复“**单元 11 完成**”，并附上：

```text
单元 11 完成
- 构建是否 0 warning：
- CTest 是否全部通过（几个测试）：
- 表驱动测试覆盖了哪些边界：
- 故意让测试失败后 CTest 是否返回非 0：
- CMake preset 命令行链是否跑通：
- Windows dist 是否包含平台插件和运行库：
- 干净环境是否能启动并导出摘要：
- 自测题有没有不会的：
```