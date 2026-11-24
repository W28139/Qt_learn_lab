# Lab 05：自定义遥测报文编解码

> **配套知识**：[单元 05 - Qt 核心类型与二进制编解码](../../05-Qt核心类型与二进制编解码.md)
> **工程位置**：`D:\Qt_\learn_demo\lab05_codec\`（Windows，用 Qt Creator 打开）
> **参考实现**：工程内的 `.cpp/.h` 和 `CMakeLists.txt`。先按本文档自己写，写完再对照。

---

## 需求

在 `D:\Qt_\learn_demo\lab05_codec\` 新建独立 CMake 工程，实现知识文档第 6 节的协议，并用一个控制台示例验证编码和解码结果。

1. **工程**
   - 工程名 `lab05_codec`，只依赖 Qt6 `Core`；
   - C++20，MSVC 下带 `/utf-8`；
   - 控制台程序：CMake 中设置 `WIN32_EXECUTABLE FALSE`；
   - 构建 0 warning。

2. **数据结构**
   - `TelemetrySample`：`quint64 seq`、`qint64 timestampMs`、`quint16 channel`、`double value`、`Quality quality`；
   - `Quality`：`Good=0`、`Warning=1`、`Bad=2`；
   - 用 `Q_DECLARE_METATYPE` 声明 `TelemetrySample` 和 `QList<TelemetrySample>`。

3. **CRC32**
   - 多项式 `0xEDB88320`，初值/结果异或 `0xFFFFFFFF`；
   - 对 `QByteArrayView` 计算；
   - 校验值：`crc32("123456789") == 0xCBF43926`。

4. **帧编码**
   - 头 20 字节：magic `0x544C`、version 1、type、sequence、timestampMs、payloadLength，全部大端；
   - 载荷见知识文档 6.3；
   - 尾部 4 字节 CRC32（大端），覆盖头 + 载荷；
   - 编码前检查：样本数 1..512、设备 ID UTF-8 字节数 ≤255、载荷 ≤64 KiB；
   - 编码失败返回 `CodecError`，不能返回“看起来像帧”的半成品。

5. **帧解码**
   - 输入 `QByteArrayView`，只接受恰好一帧；
   - 按知识文档 6.7 的顺序校验；
   - 所有读取先检查剩余长度；不得越界、不得因恶意 count 大分配；
   - 错误码至少覆盖：`TooShort`、`BadMagic`、`UnsupportedVersion`、`UnknownType`、`BadLength`、`TrailingBytes`、`BadChecksum`、`MalformedPayload`、`TooManySamples`、`EmptyBatch`、`StringTooLong`；
   - 解码失败时不修改/不交付半成品结果。

6. **输出**
   - 运行后打印 CRC 校验值、完整帧 hex 和解码后的字段；
   - 帧 hex 能和知识文档 6.4 的 golden 帧逐字节对照；
   - 不出现 Qt 警告。

## 步骤

1. 按 00/00b 的方式新建 **Qt Console Application**（或普通 CMake 工程 + `Qt6::Core`），先让 `main()` 打印一行并构建成功；
2. 写 `telemetrytypes.h`，先不写协议；
3. 写 `crc32.h/.cpp`，先用 `"123456789"` 验证校验值；
4. 写载荷编码：设备 ID + 样本数 + 样本字段，先只测“编码后再解码”的往返；
5. 写帧编码：20 字节头 + CRC，打印 hex，和知识文档 6.4 的 golden 对齐；
6. 写帧解码：先实现正常路径，再逐项加 magic/version/type/length/CRC 检查；
7. 加载荷解码：每读一个字段前加边界检查，补 count/quality/剩余字节验证；
8. 在 `main.cpp` 里造一条固定样本，编码后打印 hex，再解码回来看字段；
9. 打开编译警告，逐条清零；
10. 逐项过验收清单。

## 运行后应该看到

```text
crc32("123456789") = 0xCBF43926
frame size: 56
frame hex : 54 4C 01 01 01 02 03 04 01 02 03 04 05 06 07 08 00 00 00 20 00 01 41 00 01 00 00 00 00 00 00 00 07 00 00 01 8B CF E5 68 07 00 04 40 3E 80 00 00 00 00 00 00 4E 55 7F 65
device id: A
samples  : 1
  seq = 7 ts = 1700000000007 ch = 4 value = 30.5 quality = 0
```

`frame hex` 应该和知识文档 6.4 的 56 字节完全一致：头 20 字节 + 载荷 32 字节 + CRC 4 字节；解码出来的字段应与编码输入相同。

> Windows 控制台默认代码页可能不是 UTF-8。示例的设备 ID 用 ASCII，避免把“控制台显示乱码”误判成编解码错误；需要时可以把设备 ID 改成中文，并用 `QTextStream` 或设置控制台代码页输出。

## 可选实验（建议至少做两个）

**实验 A：JSON 体积对比**

用 `QJsonDocument` 把同一批样本写成紧凑 JSON，比较字节数。注意把 `seq` / `timestampMs` 写成字符串，避免超过 `2^53` 丢精度。再比较解析 10000 次的时间。想一想：你的场景更看重体积、速度还是可读性？

**实验 B：换成 CRC16**

用 `qChecksum()`（Qt 的 CRC-16/ISO 3309）替换 CRC32，改协议版本或校验字段宽度。比较碰撞概率和计算开销。再想一想：为什么“TCP 已经有校验”仍值得在应用层放 CRC？

**实验 C：增加 Heartbeat / Ack 消息**

复用 `encodeFrame()`，给 `Heartbeat` 定义空载荷，给 `Ack` 定义 `u32 ackSequence + u8 code + UTF-8 message`。实现对应的编解码。想一想：未知消息类型应该关闭连接还是跳过？

**实验 D：模糊测试解码器**

用 `QRandomGenerator` 生成 10 万个随机长度、随机内容的 `QByteArray`，全部喂给 `decodeFrame()`。要求不崩溃、不挂起、错误码合理；统计各错误码数量。再把随机数据中插入合法 magic/长度，测试“看起来像帧”的输入。

**实验 E：v2 协议演进**

设计 v2：在载荷尾部增加可选的 `quint32 firmwareVersion`，用 version=2 表示。让 v1 解码器明确拒绝 v2，v2 解码器兼容 v1。讨论：为什么“v1 解码器遇到 v2 时按长度跳过”可能比“直接断开”更好？跳过时如何保证不误判？

**实验 F：QDataStream 全帧对比**

用 `QDataStream` 直接序列化整个结构体/样本列表，比较字节数、跨版本读取行为和代码量。再尝试用 Python 解析它，体会“Qt 私有格式”的边界。

**实验 G：GUI hex 检查器（可选）**

用单元 03 的知识做一个小窗口：输入 hex、显示解析后的字段、错误信息。复用编解码器，不把解析逻辑写进界面。

## 验收清单

- [ ] 构建成功，0 warning；
- [ ] 工程分层：类型、CRC、协议、示例入口，协议层不依赖 Widgets/Network；
- [ ] 所有多字节字段显式大端，没有直接 `reinterpret_cast` 读未对齐整数；
- [ ] 帧头偏移与知识文档 6.2 完全一致，golden header 字节匹配；
- [ ] golden full frame（56 字节）匹配，CRC 为 `4e 55 7f 65`；
- [ ] 1 个和 512 个样本的往返逐字段相等；
- [ ] 513 个样本、空批次、超长设备 ID 被拒绝；
- [ ] 中文设备 ID 往返正确；
- [ ] `-0.0`、大时间戳、`quint64` 大序号按位保留；
- [ ] 截断、多余字节、篡改头/载荷等畸形输入返回对应错误码，不崩溃、不越界；
- [ ] 翻转载荷字节得到 `BadChecksum`；
- [ ] magic/version/type/length 错误分别得到对应错误码；
- [ ] 声明的样本数与实际字节不匹配时得到 `MalformedPayload`；
- [ ] 非法 quality 被拒绝；
- [ ] `QDataStream` 设置了 `Qt_6_0` 和大端，并检查 `status()` / `atEnd()`；
- [ ] `Q_DECLARE_METATYPE` 声明了两种类型，能解释 `qRegisterMetaType` / `QVariant` 的作用；
- [ ] 能解释为什么协议不使用 `sizeof(TelemetrySample)` 或结构体内存布局。

## 思考

1. 为什么 payloadLength 用 4 字节，而不是 2 字节？在最大 64 KiB 的限制下，2 字节也够用。哪些因素会让协议设计者选择更宽的长度字段？
2. magic 只有 2 字节，冲突概率是否足够低？如果协议跑在噪声很大的链路上，如何用 magic 重新同步？magic 变长会增加什么成本？
3. 为什么 version 和 type 要分开？如果只有 type，如何区分“旧版遥测”和“新版遥测”？
4. CRC 覆盖 header + payload，却不覆盖自己。为什么？如果只覆盖 payload，哪些错误会漏掉？
5. 如果接收方只收到半帧，`decodeFrame()` 应该返回什么？为什么它不应该自己“等更多数据”？
6. 如果长度字段被篡改成比实际小，解码器会先看到什么错误？如果比实际大呢？这两种情况分别该如何在单元 06 处理？
7. 设备 ID 长度为什么用 UTF-8 字节数而不是 `QString::size()`？一个 100 个汉字的设备名在线上占多少字节？
8. 为什么 QDataStream 的 `QString` 长度前缀对“温度”是 4，而 UTF-8 是 6？这说明了什么？
9. 如果对端不是 Qt 程序，你会选 JSON、本单元的自定义二进制，还是 protobuf？分别列出理由。
10. `QVariant` 的 `value<T>()` 转换失败时返回什么？为什么这容易掩盖 bug？
11. 单元 07 把解码放到工作线程后，跨线程信号需要哪些元类型？为什么不能传 `QByteArrayView`？
12. CRC 校验通过是否代表数据“业务上合法”？给出一个 CRC 通过但载荷仍然应当被拒绝的例子。

---

## 模块关系

```text
main.cpp
  ├── protocol.cpp     ← 帧 + 载荷编解码
  │     ├── telemetrytypes.h
  │     └── crc32.h
  └── crc32.cpp        ← 纯算法
```

关键点：

- `protocol` 不依赖 Qt Widgets、Qt Network、Qt Sql，只依赖 Qt Core；
- 编码/解码都是自由函数，没有全局状态，方便在工作线程中调用；
- 错误通过 `CodecError` 返回，不抛异常；
- 示例和实现分离，后续单元 06 可以直接复用 `protocol.*`；
- `QByteArrayView` 只出现在解码入参，不会被保存。

## 参考实现说明

工程内含 `telemetrytypes.h`、`crc32.h/.cpp`、`protocol.h/.cpp`、`main.cpp`。参考实现已在 Qt 6.4.2 + GCC 下用 `-Wall -Wextra` 构建通过（0 warning）；控制台示例打印 56 字节 golden 帧并解码还原。

建议顺序：先自己写完并过验收清单，再打开对照。重点看错误码设计、每个读取前的边界检查，以及编码前的统一校验。