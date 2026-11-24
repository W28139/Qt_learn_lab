#include "protocol.h"

#include "crc32.h"

#include <QtEndian>

#include <array>
#include <bit>

// 本文件是纯函数库：不依赖 Widgets/Network，不读文件，不抛异常。
// 所有多字节字段统一用大端（网络字节序），并且只在下面的
// appendU*/readU* 辅助函数里做端序转换——不要在别处手写端序逻辑，
// 否则很容易漏掉某个字段。
namespace proto {

namespace {

// 追加 2 字节大端整数。
// 先写进临时 buffer 再 append，避免直接往 QByteArray 尾部做未对齐写入。
void appendU16(QByteArray &out, quint16 value)
{
    std::array<char, 2> buffer{};
    qToBigEndian<quint16>(value, buffer.data());
    out.append(buffer.data(), static_cast<qsizetype>(buffer.size()));
}

// 追加 4 字节大端整数。
void appendU32(QByteArray &out, quint32 value)
{
    std::array<char, 4> buffer{};
    qToBigEndian<quint32>(value, buffer.data());
    out.append(buffer.data(), static_cast<qsizetype>(buffer.size()));
}

// 追加 8 字节大端整数。
void appendU64(QByteArray &out, quint64 value)
{
    std::array<char, 8> buffer{};
    qToBigEndian<quint64>(value, buffer.data());
    out.append(buffer.data(), static_cast<qsizetype>(buffer.size()));
}

// 从字节指针读大端整数。调用前必须用 canRead() 确认有足够字节，
// 否则会越界读取。
quint16 readU16(const char *data)
{
    return qFromBigEndian<quint16>(data);
}

quint32 readU32(const char *data)
{
    return qFromBigEndian<quint32>(data);
}

quint64 readU64(const char *data)
{
    return qFromBigEndian<quint64>(data);
}

// 边界检查：offset/count 都非负，且 [offset, offset+count) 落在 total 内。
// 用 count <= total - offset 而不是 offset + count <= total，避免加法溢出。
bool canRead(qsizetype total, qsizetype offset, qsizetype count)
{
    return offset >= 0 && count >= 0 && offset <= total && count <= total - offset;
}

// 只接受协议定义过的类型；枚举里出现其它数值一律视为非法输入。
bool isKnownType(MessageType type)
{
    return type == MessageType::TelemetryBatch || type == MessageType::Heartbeat
        || type == MessageType::Ack;
}

// 统一失败出口：写入错误码/详情并返回 false，保证每个失败都带上下文，
// 同时让解码函数可以写成 `return fail(...)` 的单行提前返回。
bool fail(CodecError *error, QString *detail, CodecError code, const QString &text)
{
    if (error)
        *error = code;
    if (detail)
        *detail = text;
    return false;
}

} // namespace

// 错误码 → 可读文本。只用于日志/界面，不参与协议传输，
// 因此文案可以随版本调整而不影响兼容性。
QString codecErrorText(CodecError error)
{
    switch (error) {
    case CodecError::None:
        return QStringLiteral("ok");
    case CodecError::TooShort:
        return QStringLiteral("frame is shorter than its declared size");
    case CodecError::BadMagic:
        return QStringLiteral("bad magic");
    case CodecError::UnsupportedVersion:
        return QStringLiteral("unsupported protocol version");
    case CodecError::UnknownType:
        return QStringLiteral("unknown message type");
    case CodecError::BadLength:
        return QStringLiteral("invalid payload length");
    case CodecError::TrailingBytes:
        return QStringLiteral("trailing bytes after frame");
    case CodecError::BadChecksum:
        return QStringLiteral("crc32 mismatch");
    case CodecError::MalformedPayload:
        return QStringLiteral("malformed payload");
    case CodecError::TooManySamples:
        return QStringLiteral("too many samples in one packet");
    case CodecError::EmptyBatch:
        return QStringLiteral("telemetry batch must not be empty");
    case CodecError::StringTooLong:
        return QStringLiteral("device id is too long");
    case CodecError::EncodeTooLarge:
        return QStringLiteral("payload exceeds the size limit");
    }
    return QStringLiteral("unknown error");
}

// 载荷布局（全部大端）：
//   u16 设备ID字节数 | 设备ID 的 UTF-8 字节 | u16 样本数 | 样本 × N
// 每个样本固定 27 字节：
//   u64 seq | u64 timestamp(位模式) | u16 channel |
//   u64 double 位模式 | u8 quality
//
// 原则：先把所有输入校验完，再开始写 buffer；失败时不返回半成品。
QByteArray encodeTelemetryPayload(const QString &deviceId,
                                  const QList<TelemetrySample> &samples,
                                  CodecError *error, QString *detail)
{
    // 每次调用先重置输出参数，避免调用者读到上一次的残留错误。
    if (error)
        *error = CodecError::None;
    if (detail)
        detail->clear();

    // 协议里的长度按“字节数”算，必须先转 UTF-8 再取 size()。
    // QString::size() 是 UTF-16 单元数，直接用它会在中文上算错。
    const QByteArray idBytes = deviceId.toUtf8();
    if (idBytes.size() > kMaxDeviceIdBytes) {
        // 超长在写入前拒绝，而不是写出一个对方无法解析的帧
        if (error)
            *error = CodecError::StringTooLong;
        if (detail)
            *detail = QStringLiteral("device id is %1 bytes, limit is %2")
                          .arg(idBytes.size())
                          .arg(kMaxDeviceIdBytes);
        return {};
    }
    // 空批次没有业务意义，也避免“0 个样本”在解码端被误判
    if (samples.isEmpty()) {
        if (error)
            *error = CodecError::EmptyBatch;
        if (detail)
            *detail = QStringLiteral("at least one sample is required");
        return {};
    }
    if (samples.size() > kMaxSamplesPerPacket) {
        if (error)
            *error = CodecError::TooManySamples;
        if (detail)
            *detail = QStringLiteral("%1 samples, limit is %2")
                          .arg(samples.size())
                          .arg(kMaxSamplesPerPacket);
        return {};
    }

    QByteArray out;
    // 提前算好总长度并 reserve，避免循环内反复扩容。
    // 前面的校验保证这个表达式不会超过协议上限。
    out.reserve(2 + idBytes.size() + 2 + samples.size() * kSampleWireSize);
    appendU16(out, static_cast<quint16>(idBytes.size()));
    out.append(idBytes);
    appendU16(out, static_cast<quint16>(samples.size()));

    for (const TelemetrySample &sample : samples) {
        appendU64(out, sample.seq);
        // qint64 的“位模式”按无符号写；这里不做数值转换，原样保留每一位。
        appendU64(out, std::bit_cast<quint64>(sample.timestampMs));
        appendU16(out, sample.channel);
        // double 同样按 IEEE 754 位模式传输：-0.0、NaN 的载荷都不会被“规范化”，
        // 也不会像字符串格式化那样引入精度/locale 问题。
        appendU64(out, std::bit_cast<quint64>(sample.value));
        // 枚举先转 quint8 再转 char：只写 1 个字节，不依赖枚举的内存布局。
        out.append(static_cast<char>(static_cast<quint8>(sample.quality)));
    }

    // 最后再确认总长度没有超过载荷上限（例如样本数合法但字段组合后仍可能超限）。
    if (static_cast<quint32>(out.size()) > kMaxPayloadSize) {
        if (error)
            *error = CodecError::EncodeTooLarge;
        if (detail)
            *detail = QStringLiteral("payload is %1 bytes, limit is %2")
                          .arg(out.size())
                          .arg(kMaxPayloadSize);
        return {};
    }
    return out;
}

// 解码载荷。核心纪律：每次读取前都先用 canRead() 确认剩余字节，
// 绝不因为“上一个字段说自己多长”就直接跳过去读。
// 所有输出参数都是可选的；并且只在全部验证成功后一次性提交，
// 调用者不会拿到“解析了一半”的结果。
bool decodeTelemetryPayload(const QByteArray &payload, QString *deviceId,
                            QList<TelemetrySample> *samples,
                            CodecError *error, QString *detail)
{
    if (error)
        *error = CodecError::None;
    if (detail)
        detail->clear();
    if (deviceId)
        deviceId->clear();
    if (samples)
        samples->clear();

    const qsizetype total = payload.size();
    // offset 是“下一个待读位置”。所有读取都通过它推进，
    // 不允许用指针对齐（未对齐访问/严格别名是 UB）。
    qsizetype offset = 0;

    // 设备 ID 长度本身占 2 字节，先确认载荷至少有这么长
    if (!canRead(total, offset, 2))
        return fail(error, detail, CodecError::MalformedPayload,
                    QStringLiteral("payload is too short for the device id length"));
    const quint16 idLength = readU16(payload.constData() + offset);
    offset += 2;

    // 不能信任这个长度：先对照业务上限，再对照剩余字节
    if (idLength > kMaxDeviceIdBytes)
        return fail(error, detail, CodecError::StringTooLong,
                    QStringLiteral("declared device id length is %1 bytes").arg(idLength));
    if (!canRead(total, offset, idLength))
        return fail(error, detail, CodecError::MalformedPayload,
                    QStringLiteral("declared device id is truncated"));
    // 线上是 UTF-8；按明确长度解码，不依赖 '\0' 结尾
    const QString id = QString::fromUtf8(payload.constData() + offset, idLength);
    offset += idLength;

    // 样本数同样先检查“能否读到 2 字节”，再读值
    if (!canRead(total, offset, 2))
        return fail(error, detail, CodecError::MalformedPayload,
                    QStringLiteral("payload is too short for the sample count"));
    const quint16 count = readU16(payload.constData() + offset);
    offset += 2;

    if (count == 0)
        return fail(error, detail, CodecError::EmptyBatch,
                    QStringLiteral("sample count is zero"));
    if (count > kMaxSamplesPerPacket)
        return fail(error, detail, CodecError::TooManySamples,
                    QStringLiteral("sample count is %1").arg(count));

    // 用“剩余字节必须恰好等于 count × 每样本字节数”一次性验证：
    // - 防止 count 过大导致循环越界；
    // - 也顺带拒绝载荷尾部多出/缺少字节的情况。
    // 先转 qsizetype 再乘，避免 16 位乘法回绕。
    const qsizetype expected = static_cast<qsizetype>(count) * kSampleWireSize;
    if (total - offset != expected)
        return fail(error, detail, CodecError::MalformedPayload,
                    QStringLiteral("payload has %1 bytes left, expected %2")
                        .arg(total - offset)
                        .arg(expected));

    QList<TelemetrySample> result;
    // 只有上面的长度校验通过后才 reserve，避免被恶意 count 触发大分配
    result.reserve(count);

    for (quint16 i = 0; i < count; ++i) {
        // 此时已确认剩余字节正好是 count × 27，循环内读取是安全的。
        TelemetrySample sample;
        sample.seq = readU64(payload.constData() + offset);
        offset += 8;
        // 读回位模式后再转回 qint64，保证负数时间戳/高位不丢
        sample.timestampMs = std::bit_cast<qint64>(readU64(payload.constData() + offset));
        offset += 8;
        sample.channel = readU16(payload.constData() + offset);
        offset += 2;
        // 按位还原 double，-0.0/NaN 等特殊值保持原样
        sample.value = std::bit_cast<double>(readU64(payload.constData() + offset));
        offset += 8;
        // char 可能带符号，先转 unsigned char 再截成 quint8
        const quint8 quality = static_cast<quint8>(
            static_cast<unsigned char>(payload.at(offset)));
        ++offset;
        // 未知枚举值不能静默当成 Good：必须拒绝，避免语义被悄悄改变
        if (quality > static_cast<quint8>(Quality::Bad))
            return fail(error, detail, CodecError::MalformedPayload,
                        QStringLiteral("invalid quality value %1 at sample %2")
                            .arg(quality)
                            .arg(i));
        sample.quality = static_cast<Quality>(quality);
        result.append(sample);
    }

    // 全部解析成功后才提交输出，调用者不会看到半成品
    if (deviceId)
        *deviceId = id;
    if (samples)
        *samples = result;
    return true;
}

// 生成完整帧：
//   头(20) + 载荷 + CRC32(4)
// 头字段固定偏移（大端）：
//   0: magic(u16)  2: version(u8)  3: type(u8)
//   4: sequence(u32)  8: timestampMs(u64)  16: payloadLength(u32)
EncodeResult encodeFrame(MessageType type, quint32 sequence, quint64 timestampMs,
                         const QByteArray &payload)
{
    EncodeResult result;
    // 不允许编码协议未定义的类型，避免对端收到无法解释的数据
    if (!isKnownType(type)) {
        result.error = CodecError::UnknownType;
        result.detail = QStringLiteral("cannot encode unknown message type");
        return result;
    }
    // 载荷上限在套头之前检查，防止生成超过协议约定的帧
    if (static_cast<quint32>(payload.size()) > kMaxPayloadSize) {
        result.error = CodecError::EncodeTooLarge;
        result.detail = QStringLiteral("payload is %1 bytes, limit is %2")
                            .arg(payload.size())
                            .arg(kMaxPayloadSize);
        return result;
    }

    QByteArray frame;
    // 一次性预留“头 + 载荷 + CRC”，后续 append 不扩容
    frame.reserve(kHeaderSize + payload.size() + kCrcSize);

    // 用固定大小的临时数组逐字段写头，偏移与上面的注释一一对应。
    // 这样不会把结构体内存布局（padding/端序）直接泄漏到线上。
    std::array<char, kHeaderSize> header{};
    qToBigEndian<quint16>(kMagic, header.data());
    header[2] = static_cast<char>(kVersion);
    header[3] = static_cast<char>(static_cast<quint8>(type));
    qToBigEndian<quint32>(sequence, header.data() + 4);
    qToBigEndian<quint64>(timestampMs, header.data() + 8);
    qToBigEndian<quint32>(static_cast<quint32>(payload.size()), header.data() + 16);

    frame.append(header.data(), kHeaderSize);
    frame.append(payload);

    // CRC 覆盖“头 + 载荷”，不包含 CRC 字段自身。
    // 解码端必须用完全相同的范围计算，否则永远匹配不上。
    const quint32 checksum = crc32(frame);
    appendU32(frame, checksum);

    result.frame = frame;
    return result;
}

// 解码一个完整帧。调用者必须已经把“一帧”的字节切出来，
// 本函数不负责从 TCP 流里找帧（那是单元 06 分帧层的职责）。
// 校验顺序刻意从便宜到贵：先排除截断，再看头字段，再核对长度，
// 最后才算 CRC——避免基于损坏的长度做分配或解析。
DecodeResult decodeFrame(QByteArrayView frame)
{
    DecodeResult result;
    // 最小合法帧 = 头 + 4 字节 CRC；载荷可以为空（比如 Heartbeat）
    if (frame.size() < kHeaderSize + kCrcSize) {
        result.error = CodecError::TooShort;
        result.detail = QStringLiteral("frame has %1 bytes, header needs at least %2")
                            .arg(frame.size())
                            .arg(kHeaderSize + kCrcSize);
        return result;
    }

    // 逐字段解析头，不 memcpy 结构体：
    // 结构体的 padding 和端序都不是线上格式的一部分。
    FrameHeader header;
    header.magic = readU16(frame.constData());
    // char → unsigned char → quint8，避免有符号 char 的符号扩展
    header.version = static_cast<quint8>(static_cast<unsigned char>(frame.at(2)));
    header.type = static_cast<MessageType>(static_cast<quint8>(
        static_cast<unsigned char>(frame.at(3))));
    header.sequence = readU32(frame.constData() + 4);
    header.timestampMs = readU64(frame.constData() + 8);
    header.payloadLength = readU32(frame.constData() + 16);

    // 以下四个检查都只依赖头本身，必须在解析载荷之前完成
    if (header.magic != kMagic) {
        result.error = CodecError::BadMagic;
        result.detail = QStringLiteral("magic is 0x%1")
                            .arg(header.magic, 4, 16, QLatin1Char('0'));
        return result;
    }
    if (header.version != kVersion) {
        result.error = CodecError::UnsupportedVersion;
        result.detail = QStringLiteral("version %1, supported %2")
                            .arg(header.version)
                            .arg(kVersion);
        return result;
    }
    if (!isKnownType(header.type)) {
        result.error = CodecError::UnknownType;
        result.detail = QStringLiteral("message type %1")
                            .arg(static_cast<quint8>(header.type));
        return result;
    }
    // 声明长度超过协议上限：直接拒绝，防止后续按巨大长度分配内存
    if (header.payloadLength > kMaxPayloadSize) {
        result.error = CodecError::BadLength;
        result.detail = QStringLiteral("declared payload length %1, limit %2")
                            .arg(header.payloadLength)
                            .arg(kMaxPayloadSize);
        return result;
    }

    // 根据声明长度算出这一帧应有的总字节数；先转 qsizetype 防溢出
    const qsizetype expectedSize = kHeaderSize + static_cast<qsizetype>(header.payloadLength)
        + kCrcSize;
    if (frame.size() < expectedSize) {
        // 数据还没收全/被截断，不能继续解析载荷
        result.error = CodecError::TooShort;
        result.detail = QStringLiteral("frame has %1 bytes, expects %2")
                            .arg(frame.size())
                            .arg(expectedSize);
        return result;
    }
    if (frame.size() > expectedSize) {
        // 帧后还有数据：说明调用者切帧切多了，或者流里混入了噪声。
        // 这里不替分帧层消费多余字节，直接报错。
        result.error = CodecError::TrailingBytes;
        result.detail = QStringLiteral("%1 unexpected bytes after frame")
                            .arg(frame.size() - expectedSize);
        return result;
    }

    // CRC 范围 = 头 + 载荷 = [0, expectedSize - 4)，不含最后 4 字节的 CRC 本身
    const quint32 expectedCrc = readU32(frame.constData() + expectedSize - kCrcSize);
    const quint32 actualCrc = crc32(
        QByteArrayView(frame.constData(), expectedSize - kCrcSize));
    if (expectedCrc != actualCrc) {
        result.error = CodecError::BadChecksum;
        result.detail = QStringLiteral("crc32 0x%1, computed 0x%2")
                            .arg(expectedCrc, 8, 16, QLatin1Char('0'))
                            .arg(actualCrc, 8, 16, QLatin1Char('0'));
        return result;
    }

    // 所有检查通过：返回头的副本和载荷的独立副本。
    // payload 必须拷贝：frame 是调用者的视图，调用后可能失效。
    result.header = header;
    result.payload = QByteArray(frame.constData() + kHeaderSize, header.payloadLength);
    return result;
}

// 组合入口：先编码载荷，再套帧。载荷编码失败时直接返回，
// 不要用空载荷继续套帧（那会生成一个“结构合法但业务为空”的帧）。
EncodeResult encodeTelemetryBatch(quint32 sequence, quint64 timestampMs,
                                  const QString &deviceId,
                                  const QList<TelemetrySample> &samples)
{
    CodecError error = CodecError::None;
    QString detail;
    const QByteArray payload = encodeTelemetryPayload(deviceId, samples, &error, &detail);
    if (error != CodecError::None) {
        EncodeResult result;
        result.error = error;
        result.detail = detail;
        return result;
    }
    return encodeFrame(MessageType::TelemetryBatch, sequence, timestampMs, payload);
}

// 组合入口：解帧 → 检查消息类型 → 解载荷。
// 先清空输出参数：调用者失败时不应拿到上一次的残留数据。
// 注意 decodeFrame() 已保证 CRC 正确，但“CRC 正确”不等于“业务合法”，
// 载荷层仍要做完整的结构校验。
DecodeResult decodeTelemetryBatch(QByteArrayView frame, QString *deviceId,
                                  QList<TelemetrySample> *samples)
{
    if (deviceId)
        deviceId->clear();
    if (samples)
        samples->clear();

    DecodeResult result = decodeFrame(frame);
    if (!result.ok())
        return result;
    // 帧本身合法，但类型不是遥测批次（例如 Heartbeat）：按未知类型拒绝
    if (result.header.type != MessageType::TelemetryBatch) {
        result.error = CodecError::UnknownType;
        result.detail = QStringLiteral("frame is not a telemetry batch");
        return result;
    }

    CodecError error = CodecError::None;
    QString detail;
    // decodeTelemetryPayload 成功时会填充 deviceId/samples；
    // 失败时把错误码/详情合并回同一个 DecodeResult
    if (!decodeTelemetryPayload(result.payload, deviceId, samples, &error, &detail)) {
        result.error = error;
        result.detail = detail;
    }
    return result;
}

} // namespace proto
