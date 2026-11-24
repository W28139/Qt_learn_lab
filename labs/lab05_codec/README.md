# Lab 05：遥测报文编解码

## 需求

1. 控制台工程 `lab05_codec`，仅依赖 `Qt6::Core`，C++20，CMake 中设置 `WIN32_EXECUTABLE FALSE`。
2. 类型：`TelemetrySample`（seq/timestampMs/channel/value/quality），`Quality` 为 Good=0、Warning=1、Bad=2；用 `Q_DECLARE_METATYPE` 声明样本和样本列表。
3. CRC32：多项式 `0xEDB88320`，初值和结果异或 `0xFFFFFFFF`，对 `QByteArrayView` 计算，`crc32("123456789") == 0xCBF43926`。
4. 帧编码：
   - 20 字节头：magic `0x544C`、version 1、type、sequence、timestampMs、payloadLength，全部大端；
   - 载荷含设备 ID（UTF-8）、样本数、样本字段；
   - 尾部 4 字节大端 CRC32，覆盖头和载荷；
   - 编码前校验样本数 1..512、设备 ID ≤255 字节、载荷 ≤64 KiB，失败返回 `CodecError`。
5. 帧解码：只接受恰好一帧；所有读取先检查剩余长度；错误码至少覆盖 TooShort、BadMagic、UnsupportedVersion、UnknownType、BadLength、TrailingBytes、BadChecksum、MalformedPayload、TooManySamples、EmptyBatch、StringTooLong；失败时不交付半成品。
6. 输出：打印 CRC 值、完整帧 hex 和解码字段。golden 帧为 56 字节，CRC `4e 55 7f 65`，hex 应逐字节一致。

## 可选实验

- 与紧凑 JSON 比较体积和解析耗时；
- 用 `QRandomGenerator` 生成随机字节模糊测试解码器，要求不崩溃、错误码合理；
- 设计 v2 协议并兼容 v1。

## 验收

- [ ] 0 warning；协议层不依赖 Widgets/Network；
- [ ] 多字节字段显式大端，没有未对齐的 `reinterpret_cast`；
- [ ] golden 帧逐字节匹配；1 个和 512 个样本往返一致；
- [ ] 513 个样本、空批次、超长设备 ID 被拒绝；
- [ ] 截断、篡改、错误 magic/version/type/length 返回对应错误码，不崩溃、不越界；
- [ ] 翻转载荷得到 `BadChecksum`，非法 quality 被拒绝；
- [ ] 能解释为什么不用 `sizeof(TelemetrySample)` 描述线上格式。

参考实现见本目录。