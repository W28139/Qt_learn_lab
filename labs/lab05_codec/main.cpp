#include "crc32.h"
#include "protocol.h"

#include <QCoreApplication>
#include <QDebug>
#include <QList>
#include <QString>

namespace {

// 固定的一份样本，方便和知识文档 6.4 的 golden 帧逐字节对照。
TelemetrySample makeGoldenSample()
{
    TelemetrySample sample;
    sample.seq = 7;
    sample.timestampMs = 1700000000007LL;
    sample.channel = 4;
    sample.value = 30.5;
    sample.quality = Quality::Good;
    return sample;
}

} // namespace

// 控制台示例：编码一个遥测帧，打印字节，再解码回来看字段。
// 只依赖 Qt Core：不创建窗口、不碰网络，协议层可以独立运行。
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // 1. CRC 校验值：和知识文档里给的标准值对照
    const quint32 check = crc32(QByteArrayLiteral("123456789"));
    const QString checkText = QString::number(check, 16)
                                  .rightJustified(8, QLatin1Char('0'))
                                  .toUpper();
    qInfo().noquote() << QStringLiteral("crc32(\"123456789\") = 0x") + checkText;

    // 2. 编码：设备 ID "A" + 一条样本，就是知识文档 6.4 的 56 字节帧
    const TelemetrySample sample = makeGoldenSample();
    const EncodeResult encoded = proto::encodeTelemetryBatch(
        0x01020304u, 0x0102030405060708ULL, QStringLiteral("A"),
        QList<TelemetrySample>{sample});
    if (!encoded.ok()) {
        qCritical().noquote() << "encode failed:" << encoded.detail;
        return 1;
    }

    qInfo().noquote() << "frame size:" << encoded.frame.size();
    qInfo().noquote() << "frame hex :"
                      << QString::fromLatin1(encoded.frame.toHex(' ').toUpper());

    // 3. 解码：把刚编码出的字节再解析回来
    QString deviceId;
    QList<TelemetrySample> samples;
    const DecodeResult decoded = proto::decodeTelemetryBatch(
        QByteArrayView(encoded.frame), &deviceId, &samples);
    if (!decoded.ok()) {
        qCritical().noquote() << "decode failed:" << decoded.detail;
        return 1;
    }

    qInfo().noquote() << "device id:" << deviceId;
    qInfo().noquote() << "samples  :" << samples.size();
    for (const TelemetrySample &item : samples) {
        qInfo().noquote() << "  seq =" << item.seq
                          << "ts =" << item.timestampMs
                          << "ch =" << item.channel
                          << "value =" << item.value
                          << "quality =" << static_cast<int>(item.quality);
    }
    return 0;
}