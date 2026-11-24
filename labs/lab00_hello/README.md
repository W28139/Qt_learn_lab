# Lab 00：在 Windows 上跑起第一个 Qt 6 窗口

> **配套知识**：[单元 00 - 环境与第一个 Qt6 窗口](../../00-环境与第一个Qt6窗口.md)
> **工程位置**：`D:\Qt_\learn_demo\lab00_hello\`（Windows，用 Qt Creator 打开）
> **参考实现**：工程内的 `.cpp/.h` 和 `CMakeLists.txt`。先按本文档自己写，写完再对照。

---

## 目标

确认 Kit、CMake、Qt 版本都正确，能独立创建并运行一个 Qt 6 Widgets 工程。

## 任务

在 `D:\Qt_\learn_demo\lab00_hello\` 创建工程。

1. 新建工程：
   - `CMakeLists.txt`：使用知识文档第 3 节的模板，把工程名改成 `lab00_hello`；
   - `main.cpp`：按知识文档第 4 节写出第一个窗口。
2. 用 Qt Creator 打开：
   - `文件 → 打开文件或项目` → 选择 `CMakeLists.txt`；
   - 选择你的 **Desktop Qt 6.8.x MSVC2022 64bit** Kit → `Configure Project`。
3. 按 `Ctrl+B` 构建，`Ctrl+R` 运行。
4. 看到窗口后，做以下修改并重新运行：
   - 把应用版本改成 `0.1.0`；
   - 确认窗口标题变成 `Lab00 0.1.0`；
   - 把窗口大小改成 `520 x 260`。
5. **实验（必做）**：临时把 `return app.exec();` 改成 `return 0;`，重新运行，观察窗口行为，然后改回来。
6. 在窗口显示时，确认“运行时 Qt 版本”和“编译时 Qt 版本”一致。

## 验收清单

- [ ] `Ctrl+B` 构建成功，**0 warning**；
- [ ] `Ctrl+R` 后窗口正常显示，文字居中；
- [ ] 标题是 `Lab00 0.1.0`；
- [ ] 运行时与编译时 Qt 版本一致，且都是 6.x；
- [ ] C++ 标准值 ≥ `202002L`；
- [ ] 去掉 `exec()` 时窗口一闪而过，能解释原因；
- [ ] 关闭窗口后进程正常退出；
- [ ] （可选）没有黑色控制台窗口。

## 完成后

按 [README](../../README.md) 的规则回复“**单元 0 完成**”，并附上 Qt 实际版本、Kit、是否 0 warning。