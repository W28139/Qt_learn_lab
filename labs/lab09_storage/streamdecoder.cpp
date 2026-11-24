#include "streamdecoder.h"

#include <QtEndian>

#include <algorithm>
#include <cstring>

// 构造：确保缓冲区上限不小于最小帧尺寸（头 + CRC）
StreamDecoder::StreamDecoder(qsizetype maxBufferBytes)
    : m_maxBufferBytes(std::max<qsizetype>(maxBufferBytes,
                                           proto::kHeaderSize + proto::kCrcSize)){}

// 追加字节；已失败或空数据直接忽略
void StreamDecoder::append(QByteArrayView data)
{
    if (m_failed || data.isEmpty())
        return;
    m_buffer.append(data);
}

// 重置：清空缓冲、计数与失败状态（重连时使用）
void StreamDecoder::clear()
{
    m_buffer.clear();
    m_discarded = 0;
    m_failed = false;
    m_errorDetail.clear();
}

// 丢弃缓冲区前 count 字节，并累计到丢弃计数
void StreamDecoder::discard(qsizetype count)
{
    if (count <= 0)
        return;
    const qsizetype n = std::min(count, m_buffer.size());
    m_buffer.remove(0, n);
    m_discarded += static_cast<quint64>(n);
}

// 进入不可恢复失败状态，并返回 ProtocolError 结果
StreamResult StreamDecoder::fatal(CodecError error, const QString &detail)
{
    m_failed = true;
    m_errorDetail = detail;
    StreamResult result;
    result.status = StreamStatus::ProtocolError;
    result.frame.error = error;
    result.frame.detail = detail;
    result.detail = detail;
    return result;
}

// 核心：从缓冲区头部尝试解析出一个完整帧
StreamResult StreamDecoder::nextFrame()
{
    // 已失败：重复返回错误
    if (m_failed) {
        StreamResult result;
        result.status = StreamStatus::ProtocolError;
        result.frame.error = CodecError::MalformedPayload;
        result.frame.detail = m_errorDetail;
        result.detail = m_errorDetail;
        return result;
    }

    // 数据不足以判定帧头
    if (m_buffer.size() < proto::kHeaderSize)
        return StreamResult{};

    // 搜索魔数 0x54 0x4C ("TL")，跳过前面的无效字节
    qsizetype magicPos = -1;
    for (qsizetype i = 0; i + 1 < m_buffer.size(); ++i) {
        const auto *bytes = reinterpret_cast<const unsigned char *>(m_buffer.constData() + i);
        if (bytes[0] == 0x54 && bytes[1] == 0x4C) {
            magicPos = i;
            break;
        }
    }

    if (magicPos < 0) {
        // 未找到魔数：丢弃全部，但保留末尾可能是魔数首字节的 0x54
        const qsizetype keep = (!m_buffer.isEmpty()
                                && static_cast<unsigned char>(m_buffer.back()) == 0x54)
                                   ? 1
                                   : 0;
        discard(m_buffer.size() - keep);
        return StreamResult{};
    }

    if (magicPos > 0)
        discard(magicPos);   // 丢弃魔数之前的无效字节

    if (m_buffer.size() < proto::kHeaderSize)
        return StreamResult{};

    // 解析帧头字段
    const char *header = m_buffer.constData();
    const quint8 version = static_cast<quint8>(static_cast<unsigned char>(header[2]));
    const quint8 rawType = static_cast<quint8>(static_cast<unsigned char>(header[3]));
    const quint32 payloadLength = qFromBigEndian<quint32>(header + 16); // 大端负载长度

    // 版本校验
    if (version != proto::kVersion) {
        return fatal(CodecError::UnsupportedVersion,
                     QStringLiteral("stream version %1, supported %2")
                         .arg(version)
                         .arg(proto::kVersion));
    }
    // 消息类型校验
    if (rawType < static_cast<quint8>(MessageType::TelemetryBatch)
        || rawType > static_cast<quint8>(MessageType::Ack)) {
        return fatal(CodecError::UnknownType,
                     QStringLiteral("stream message type %1").arg(rawType));
    }
    // 负载长度上限校验
    if (payloadLength > proto::kMaxPayloadSize) {
        return fatal(CodecError::BadLength,
                     QStringLiteral("declared payload length %1, limit %2")
                         .arg(payloadLength)
                         .arg(proto::kMaxPayloadSize));
    }

    // 计算整帧大小（头 + 负载 + CRC），并校验是否超过缓冲区上限
    const qsizetype frameSize = proto::kHeaderSize
                                + static_cast<qsizetype>(payloadLength) + proto::kCrcSize;
    if (frameSize > m_maxBufferBytes) {
        return fatal(CodecError::BadLength,
                     QStringLiteral("frame of %1 bytes exceeds buffer limit %2")
                         .arg(frameSize)
                         .arg(m_maxBufferBytes));
    }
    // 帧未收全：等待更多数据
    if (m_buffer.size() < frameSize)
        return StreamResult{};

    // 取出整帧字节并移出缓冲
    const QByteArray frameBytes = m_buffer.left(frameSize);
    m_buffer.remove(0, frameSize);

    // 交给 proto 层做 CRC/解码校验
    DecodeResult decoded = proto::decodeFrame(QByteArrayView(frameBytes));
    if (!decoded.ok()) {
        m_failed = true;
        m_errorDetail = decoded.detail;

        StreamResult result;
        result.status = StreamStatus::ProtocolError;
        result.frame = decoded;
        result.detail = decoded.detail;
        return result;
    }

    // 成功解出一帧
    StreamResult result;
    result.status = StreamStatus::FrameReady;
    result.frame = std::move(decoded);
    return result;
}