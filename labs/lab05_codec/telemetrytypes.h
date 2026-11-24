#pragma once

#include <QList>
#include <QMetaType>
#include <QtGlobal>

// 样本质量。显式指定底层类型 quint8，保证线上就是 1 字节，
enum class Quality : quint8 {
    Good = 0,
    Warning = 1,
    Bad = 2,
};

// 单个遥测样本：纯数据结构，不继承 QObject，不依赖界面。
// 这个结构体只用于内存表示，线上格式要逐字段编码
struct TelemetrySample
{
    quint64 seq = 0;          // 样本序号，64 位，超过 2^32 也不能回绕
    qint64 timestampMs = 0;   // Unix 毫秒时间戳
    quint16 channel = 0;      // 通道号
    double value = 0.0;       // 测量值，按 IEEE 754 位模式原样传输
    Quality quality = Quality::Good;
};

// 让这两个类型可以被 QVariant 持有、被信号槽按值传递。
// 如果要用按名字的队列连接，还需要运行时 qRegisterMetaType（见单元 07）。
Q_DECLARE_METATYPE(TelemetrySample)
Q_DECLARE_METATYPE(QList<TelemetrySample>)