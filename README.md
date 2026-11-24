# Qt 快速通道：从零到能独立写一个 Qt Widgets 客户端

> 目标：用最短路径掌握 Qt 桌面客户端开发所需的核心能力，最终能独立实现一个带实时数据接收、界面展示、网络通信和本地存储的 Qt Widgets 应用。
>
> 不追求：Qt 全栈、QML、Qt Designer、Qt Charts、插件系统、Qt 源码剖析。

---

## 1. 我们对齐过的约束

| 维度 | 你的情况 | 对课程的影响 |
|---|---|---|
| C++ 基础 | C++ 后端，线程、网络、Linux 都熟 | Qt 不讲 C++ 语法，直接讲 Qt 的模型和坑 |
| Qt 基础 | 零基础 | 从对象模型、事件循环讲起 |
| 写代码平台 | **Windows**（Qt Creator + Qt 6.8.x） | 所有步骤按 Windows 操作写 |
| 文档位置 | WSL2 `/home/wevix/qt_learn` | 文档放这里，代码在 Windows 写 |
| 学习深度 | **辅助技能，够用就行** | 不展开设计模式、不做通用库、不卷界面美化 |
| 时间投入 | 每天 3 小时以上 | 约 12 个单元，每单元 2.5～3.5 小时 |

> **关于版本号**：你说 Qt 版本是“8 点几”，有两种可能：
> - **Qt 版本 6.8.x**（最常见）；
> - **Qt Creator 版本 18.x**（IDE 版本，和 Qt 库版本不是一回事）。
>
> 单元 00 第一件事就是核对。本课程要求 **Qt ≥ 6.4**，6.8 完全可用。课程代码统一按 **Qt 6.4+ API** 写，避免 6.5+ 新接口，这样以后在 Qt 6.4 环境中也不会编译失败。

---

## 2. 为什么这样学，而不是找套 Qt5 视频课

你现在的处境是：**有时间、有 C++ 功底、但目标是尽快具备做一个桌面应用的能力**，不是转行做 Qt 客户端工程师。完整 Qt 课程的问题：

1. 大量时间花在 QML/Qt Quick、Designer、数据库模型高级用法、国际化等本课程不用的内容上；
2. 多数仍在讲 Qt5 + qmake，和本课程的 **Qt6 + CMake + 纯代码 UI** 对不上；
3. 先学完再动手，反馈周期太长，容易半途而废。

所以本课程是**目标驱动**的：先确定一个典型桌面客户端需要哪些能力（窗口与布局、网络收发、模型视图、线程、绘图、数据库、测试与打包），再倒推出最短学习路径，每个单元都配一个能独立运行的 Lab。

---

## 3. 学习规则（请严格执行）

1. **一次只读一个单元**，不要提前翻后面的文档。
2. **Lab 必须自己敲**，不要复制粘贴参考实现。Lab 的要求写在独立的 Lab 文档里，参考实现是 lab 工程里的 `.cpp/.h`，写完再看。
3. Lab 的**验收清单全部打勾**，再进入下一单元。
4. 编译必须 **0 warning**。警告在这个阶段就是未来的 bug。
5. 卡住超过 30 分钟，把以下内容发给我：
   - 完整报错（不要只截一行）；
   - 你的 `CMakeLists.txt`；
   - 出问题的最小 `main.cpp`；
   - Qt / Qt Creator / 编译器版本。
6. 每完成一个单元，回来说一句“**单元 N 完成**”，我会：
   - 根据你的完成情况调整下一单元；
   - 释放后续单元的详细文档（目前**已放出 00～11**，之后**一次只发一个**，避免信息量劝退）。

---

## 4. 目录与路径约定

| 内容 | 位置 |
|---|---|
| 知识文档 | WSL2：`/home/wevix/qt_learn/NN-单元名.md` |
| Lab 文档 | WSL2：`/home/wevix/qt_learn/labs/labNN_xxx/README.md` |
| Lab 代码 | Windows：`D:\Qt_\learn_demo\labNN_xxx\`（例如 `D:\Qt_\learn_demo\lab00_hello`） |
| Windows 上查看文档 | VS Code 远程 WSL，或资源管理器打开 `\\wsl.localhost\Ubuntu\home\wevix\qt_learn` |
| 每个 Lab | **独立 CMake 工程**，互不依赖，可以单独构建运行 |
| 参考实现 | 对应 lab 工程里的 `.cpp/.h` 和 `CMakeLists.txt`；知识文档和 Lab 文档里不再内嵌完整代码 |
| 构建 | 优先用 Qt Creator 的“Build / Run”（Ctrl+B / Ctrl+R） |
| 构建目录 | 交给 Qt Creator 管理（默认在源码同级 `build-labXX-...`） |

**技术基线：**

- Qt 6.4+（你机器上是 6.8.x 即可）
- C++20
- Qt 模块：`Core`、`Widgets`、`Network`、`Sql`、`Test`
- 不使用 `.ui` 文件、不使用 Designer、不使用 QML
- 界面全部用 C++ 代码写

---

## 5. 课程地图

| # | 知识文档 | 主题 | Lab 文档 | 核心产出 | 预计 |
|---|---|---|---|---|---|
| 00 | [环境与第一个 Qt 6 窗口](00-环境与第一个Qt6窗口.md) | Kit、CMake、Qt6 模块、第一个窗口 | [Lab 00](labs/lab00_hello/README.md) | Hello 窗口 | 2.5h |
| 01 | [对象模型、事件循环与定时器](01-对象模型事件循环与定时器.md) | QObject、父子对象、事件循环、QTimer | [Lab 01](labs/lab01_timer/README.md) | 定时器小应用 | 3h |
| 02 | [信号槽与跨线程语义](02-信号槽与跨线程语义.md) | connect 四种写法、AutoConnection、队列连接 | [Lab 02](labs/lab02_signals/README.md) | 跨线程信号 demo | 3h |
| 03 | [Widgets 布局与主窗口骨架](03-Widgets布局与主窗口骨架.md) | QMainWindow、布局、Tab、状态栏、对话框 | [Lab 03](labs/lab03_skeleton/README.md) | 仿制连接页 | 3h |
| 04 | [Model/View 与高频刷新](04-ModelView与高频刷新.md) | QAbstractTableModel、环形缓冲、批量刷新 | [Lab 04](labs/lab04_telemetry/README.md) | 100Hz 遥测表格 | 3h |
| 05 | [Qt 核心类型与二进制编解码](05-Qt核心类型与二进制编解码.md) | QString/QByteArray/QDataStream、端序、元类型 | [Lab 05](labs/lab05_codec/README.md) | 自定义报文编解码 | 3h |
| 06 | [Qt Network 与流式解包](06-Qt%20Network%20与流式解包.md) | QTcpSocket/QTcpServer、粘包拆包 | [Lab 06](labs/lab06_stream/README.md) | 分帧服务端 + 客户端 | 3.5h |
| 07 | [QThread 与工作线程模型](07-QThread与工作线程模型.md) | moveToThread、线程安全队列、优雅退出 | [Lab 07](labs/lab07_worker/README.md) | 网络 worker | 3.5h |
| 08 | [QPainter 自绘趋势图](08-QPainter自绘趋势图.md) | paintEvent、坐标映射、滑动窗口 | [Lab 08](labs/lab08_trendchart/README.md) | 实时曲线 | 3h |
| 09 | [SQLite 持久化](09-SQLite持久化.md) | 每线程连接、事务、批量写、分页查询 | [Lab 09](labs/lab09_storage/README.md) | 遥测落盘 + 查询 | 3.5h |
| 10 | [文件、配置、CSV 与日志](10-文件配置CSV与日志.md) | QStandardPaths、QSettings、日志分类与轮转 | [Lab 10](labs/lab10_files/README.md) | 日志/导出模块 | 3h |
| 11 | [Qt Test 与 Windows 构建打包](11-QtTest与Windows构建打包.md) | Qt Test、CTest、windeployqt、CMakePresets | [Lab 11](labs/lab11_release/README.md) | 可发布 exe | 3h |

> 合计约 37 小时。按每天 3 小时算，**约 12 天**学完；之后上手实际项目会快很多。

> **配套速查（不算单元，随时可查）**：[00b - Qt Creator 使用与工程创建速查](00b-QtCreator使用与工程创建速查.md)
> 讲的是 Qt Creator 怎么装、怎么认、怎么建工程、怎么构建运行，以及常见报错速查。第一次动手前先看这份。

---

## 6. 课程覆盖的 Qt 能力

```text
Qt Core      QObject / 事件循环 / QTimer / QString / QByteArray / QDataStream
             QVariant / QDateTime / 容器 / QThread / QMutex / 文件与路径
Qt Gui       QPainter / QFont / QColor / QImage（只用到绘图）
Qt Widgets   QMainWindow / 布局 / QLabel / QPushButton / QTableView / QTabWidget
             QStatusBar / QMessageBox / QFileDialog
Qt Network   QTcpSocket / QTcpServer
Qt Sql       QSqlDatabase / QSqlQuery / SQLite
Qt Test      QTest / QCOMPARE / QVERIFY / 数据驱动测试
CMake        Qt6 的 find_package / qt_add_executable / CMakePresets
部署          windeployqt / MSVC 运行库
```

**明确不学：**

- QML / Qt Quick（本课程不用）
- Qt Designer / `.ui` 文件（本课程是纯代码 UI）
- Qt Charts（本课程用 QPainter 自绘）
- 多语言翻译、插件、样式表深水区
- Qt 容器/模型的高级定制（只学够用）

---

## 7. 每个单元的结构

每个单元拆成三部分，各管一件事：

**知识文档**（`NN-单元名.md`）—— 只讲知识，不塞完整实现：

1. **知识点**：只讲本单元必须知道的，配极短的 API 示例和反例；
2. **自测题**：能答出来才算真的会；
3. **常见坑**：Windows/Qt6 特有的问题。

**Lab 文档**（`labs/labNN_xxx/README.md`）—— 只讲任务，不写代码：

1. **需求**：要实现什么、验收标准是什么；
2. **步骤**：推荐的实现顺序；
3. **运行后应该看到**：自检用的现象；
4. **可选实验**：加深理解的改动；
5. **验收清单**：全部打勾再进下一单元；
6. **思考**：做完之后回答的问题。

**Lab 代码**（`D:\Qt_\learn_demo\labNN_xxx\`）—— 真正的 `.cpp/.h`：

- 每个 Lab 是独立 CMake 工程；
- 参考实现就在工程里，写完自己的版本再对照；
- 后续单元直接复用前面工程的代码，不在文档里复制粘贴。

---

## 8. 查资料的正确姿势

1. **Qt Creator 里按 F1**：光标放在类名上按 F1，直接打开 Qt Assistant，比搜索引擎快。
2. 官方文档：<https://doc.qt.io/qt-6/>，只看类参考，不要通读模块说明。
3. 搜索时带上 `Qt6` 和类名，例如 `Qt6 QTcpSocket readyRead`；Qt5 的结果大部分仍然有效，但注意第 8 节列出的差异。
4. 以 Qt 官方文档和本课程为准，网上教程只作参考。

### Qt5 → Qt6 常见差异（照抄老教程时注意）

| Qt5 写法 | Qt6 写法 |
|---|---|
| `QT += widgets`（qmake） | CMake：`find_package(Qt6 ... Widgets)` |
| `QString::SkipEmptyParts` | `Qt::SkipEmptyParts` |
| `QTextCodec` | `QString::fromUtf8()` / `QStringDecoder` |
| `QRegExp` | `QRegularExpression` |
| `qAsConst(x)` | `std::as_const(x)` |
| `QDesktopWidget` | `QScreen` |
| `QVector` / `QList` 分开 | Qt6 中二者已统一为 `QList` |
| 手动开启高 DPI 缩放 | Qt6 默认开启，旧属性已废弃 |
| `SIGNAL()` / `SLOT()` 宏 | 新式 `&Class::signal` 语法（本课程只用新式） |

---

## 9. 现在开始

1. 如果还没在 Qt Creator 里建过工程，先读 **[00b：Qt Creator 使用与工程创建速查](00b-QtCreator使用与工程创建速查.md)**（操作手册，20 分钟）；
2. 然后打开 **[单元 00：环境与第一个 Qt 6 窗口](00-环境与第一个Qt6窗口.md)** 和 **[Lab 00](labs/lab00_hello/README.md)**，代码写到 `D:\Qt_\learn_demo\lab00_hello\`。

完成 Lab 00 后回来告诉我：

```text
单元 0 完成
- Qt 实际版本：
- 编译器/Kit：
- 构建是否 0 warning：
- 自测题有没有不会的：
```

然后进入 **[单元 01：对象模型、事件循环与定时器](01-对象模型事件循环与定时器.md)** 和 **[Lab 01](labs/lab01_timer/README.md)**。完成单元 01 后，进入 **[单元 02：信号槽与跨线程语义](02-信号槽与跨线程语义.md)** 和 **[Lab 02](labs/lab02_signals/README.md)**。完成单元 02 后，进入 **[单元 03：Widgets 布局与主窗口骨架](03-Widgets布局与主窗口骨架.md)** 和 **[Lab 03](labs/lab03_skeleton/README.md)**。完成单元 03 后，进入 **[单元 04：Model/View 与高频刷新](04-ModelView与高频刷新.md)** 和 **[Lab 04](labs/lab04_telemetry/README.md)**。完成单元 04 后，进入 **[单元 05：Qt 核心类型与二进制编解码](05-Qt核心类型与二进制编解码.md)** 和 **[Lab 05](labs/lab05_codec/README.md)**。完成单元 05 后，进入 **[单元 06：Qt Network 与流式解包](06-Qt%20Network%20与流式解包.md)** 和 **[Lab 06](labs/lab06_stream/README.md)**。完成单元 06 后，进入 **[单元 07：QThread 与工作线程模型](07-QThread与工作线程模型.md)** 和 **[Lab 07](labs/lab07_worker/README.md)**。完成单元 07 后，进入 **[单元 08：QPainter 自绘趋势图](08-QPainter自绘趋势图.md)** 和 **[Lab 08](labs/lab08_trendchart/README.md)**。完成单元 08 后，进入 **[单元 09：SQLite 持久化](09-SQLite持久化.md)** 和 **[Lab 09](labs/lab09_storage/README.md)**。完成单元 09 后，进入 **[单元 10：文件、配置、CSV 与日志](10-文件配置CSV与日志.md)** 和 **[Lab 10](labs/lab10_files/README.md)**。完成单元 10 后，进入 **[单元 11：Qt Test 与 Windows 构建打包](11-QtTest与Windows构建打包.md)** 和 **[Lab 11](labs/lab11_release/README.md)**。完成单元 11 后，回来告诉我“**单元 11 完成**”，课程主体到此结束；下一步可以把前面单元合成一个完整客户端，或按你的实际项目需求补专题。



# Qt_learn_lab
