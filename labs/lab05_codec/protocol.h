#pragma once

#include "telemetrytypes.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QString>

// 帧头里的消息类型。数值是协议的一部分，发布后不能改含义；
// 新增类型只能往后加
enum class MessageType : quint8 {
    Unknown = 0,         // 保留值：0 永远不当作合法类型
    TelemetryBatch = 1,  // 本 Lab 使用的遥测批量数据
    Heartbeat = 2,       // 预留：保活
    Ack = 3,             // 预留：确认
};

// 编解码错误码。
// 不用异常：异常跨越 Qt 事件循环/线程边界容易变成 std::terminate，而且错误码更方便日志记录、统计和跨语言实现。
enum class CodecError {
    None = 0,             // 成功
    TooShort,             // 实际字节少于声明长度（截断）
    BadMagic,             // 魔数不对，不是本协议数据
    UnsupportedVersion,   // 协议版本不认识
    UnknownType,          // 消息类型不认识
    BadLength,            // 长度字段超过协议上限
    TrailingBytes,        // 帧后有多余字节（应由分帧层处理）
    BadChecksum,          // CRC32 不匹配
    MalformedPayload,     // 载荷内部结构非法
    TooManySamples,       // 样本数超过上限
    EmptyBatch,           // 空批次
    StringTooLong,        // 设备 ID 超长
    EncodeTooLarge,       // 编码结果超过载荷上限
};

// 帧头的逻辑结构（线上固定 20 字节，大端）。
// 这与 encodeFrame()/decodeFrame() 的逐字段读写保持一致，
// 但绝不能把它的内存布局直接 memcpy 到线上（存在 padding）。
struct FrameHeader
{
    quint16 magic = 0;                        // 0x544C（'T''L'），识别与重同步
    quint8 version = 0;                       // 协议版本，当前为 1
    MessageType type = MessageType::Unknown;  // 载荷类型
    quint32 sequence = 0;                     // 序号，用于丢包检测/请求响应关联
    quint64 timestampMs = 0;                  // 发送端 Unix 毫秒时间戳
    quint32 payloadLength = 0;                // 载荷字节数（不含头，也不含 CRC）
};

// 解码结果：错误码 + 头 + 载荷副本 + 可读详情。
// 失败时不要使用 header/payload，它们可能仍是默认值
struct DecodeResult
{
    CodecError error = CodecError::None;
    FrameHeader header;
    QByteArray payload;
    QString detail;

    bool ok() const { return error == CodecError::None; }
};

// 编码结果：成功时 frame 是完整帧（头 + 载荷 + CRC）；
// 失败时 frame 为空，error/detail 说明原因。
struct EncodeResult
{
    QByteArray frame;
    CodecError error = CodecError::None;
    QString detail;

    bool ok() const { return error == CodecError::None; }
};

namespace proto {

// 协议常量集中定义：编码端、解码端和示例共用同一个数字，
// 避免两边各写一份导致不一致。
inline constexpr quint16 kMagic = 0x544C; // 'T', 'L'
inline constexpr quint8 kVersion = 1;
// 头 = magic(2) + version(1) + type(1) + seq(4) + timestamp(8) + length(4)
inline constexpr qsizetype kHeaderSize = 20;
inline constexpr qsizetype kCrcSize = 4;                // CRC 校验值占 4 个字节
inline constexpr quint32 kMaxPayloadSize = 64 * 1024;   // 单个载荷（payload）的最大字节数
inline constexpr quint16 kMaxSamplesPerPacket = 512;    // 每个数据包（packet）最多能携带 512 个样本
inline constexpr qsizetype kMaxDeviceIdBytes = 255;     // UTF-8 字节数，不是字符数
// 样本线上大小 = seq(8) + timestamp(8) + channel(2) + value(8) + quality(1)
inline constexpr qsizetype kSampleWireSize = 27;

// 把设备 ID + 样本列表编码为“载荷”（不含帧头/CRC）
QByteArray encodeTelemetryPayload(const QString &deviceId,
                                  const QList<TelemetrySample> &samples,
                                  CodecError *error, QString *detail);

// 解码载荷，全部字段验证通过后才写入 deviceId/samples
bool decodeTelemetryPayload(const QByteArray &payload, QString *deviceId,
                            QList<TelemetrySample> *samples,
                            CodecError *error, QString *detail);

// 给载荷套上 20 字节帧头并追加 CRC32，生成完整帧。
EncodeResult encodeFrame(MessageType type, quint32 sequence, quint64 timestampMs,
                         const QByteArray &payload);

// 解码一个“完整帧”；调用者只传入一帧的字节，不负责从流里拆包。
DecodeResult decodeFrame(QByteArrayView frame);

// 编码便捷入口：encodeTelemetryPayload() + encodeFrame()，
// 两步的错误统一收进一个 EncodeResult。
EncodeResult encodeTelemetryBatch(quint32 sequence, quint64 timestampMs,
                                  const QString &deviceId,
                                  const QList<TelemetrySample> &samples);

// 解码便捷入口：decodeFrame() → 确认类型为 TelemetryBatch → decodeTelemetryPayload()。
DecodeResult decodeTelemetryBatch(QByteArrayView frame, QString *deviceId,
                                  QList<TelemetrySample> *samples);

// 错误码转可读文本，仅用于日志/界面提示，不参与协议传输。
QString codecErrorText(CodecError error);

} // namespace proto