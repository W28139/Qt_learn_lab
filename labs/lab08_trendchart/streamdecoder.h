#pragma once

#include "protocol.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

// 流式解码状态
enum class StreamStatus {
    NeedMoreData = 0,   // 数据不足，需要继续追加字节
    FrameReady,         // 成功解析出一帧
    ProtocolError,      // 协议错误（不可恢复）
};

// 单次 nextFrame() 的返回结果
struct StreamResult
{
    StreamStatus status = StreamStatus::NeedMoreData; // 解码状态
    DecodeResult frame;                               // 解析出的帧（仅 FrameReady 时有效）
    QString detail;                                   // 出错时的描述信息

    bool ok() const { return status == StreamStatus::FrameReady; } // 是否成功出帧
};

// 字节流 → 帧 的流式解码器：
// 不断 append() 收到的字节，反复调用 nextFrame() 取出完整帧；
// 自动丢弃帧头之前的无效字节，并限制缓冲区上限，超限即判定失败。
class StreamDecoder
{
public:
    // maxBufferBytes: 缓冲区上限（默认 1MB），超过视为协议错误
    explicit StreamDecoder(qsizetype maxBufferBytes = 1024 * 1024);

    void append(QByteArrayView data);  // 追加新收到的字节
    StreamResult nextFrame();          // 尝试解析下一帧

    void clear();                      // 清空缓冲区（重连时调用）

    qsizetype bufferedBytes() const { return m_buffer.size(); }   // 当前缓冲区字节数
    quint64 discardedBytes() const { return m_discarded; }        // 已丢弃的无效字节数
    bool failed() const { return m_failed; }                      // 是否已进入不可恢复失败状态

private:
    void discard(qsizetype count);    // 丢弃缓冲区前 count 字节并累计计数
    StreamResult fatal(CodecError error, const QString &detail); // 标记失败并返回 ProtocolError

    QByteArray m_buffer;               // 未消费的字节缓冲
    qsizetype m_maxBufferBytes;        // 缓冲区上限
    quint64 m_discarded = 0;           // 累计丢弃字节数
    bool m_failed = false;             // 是否已失败（失败后不再继续解析）
    QString m_errorDetail;             // 失败原因
};