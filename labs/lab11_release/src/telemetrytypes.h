#pragma once

#include <QtGlobal>

// 样本质量。显式指定底层类型 quint8，保证线上就是 1 字节，
// 不随编译器/平台的枚举大小变化。
enum class Quality : quint8 {
    Good = 0,
    Warning = 1,
    Bad = 2,
};

// 单个遥测样本：纯数据结构，不继承 QObject，不依赖界面。
// 注意：这个结构体只用于内存表示，线上格式要逐字段编码，
// 不能把 sizeof(TelemetrySample) 当作协议长度（有内存对齐 padding）。
struct TelemetrySample
{
    quint64 seq = 0;          // 样本序号，64 位，超过 2^32 也不能回绕
    qint64 timestampMs = 0;   // Unix 毫秒时间戳
    quint16 channel = 0;      // 通道号
    double value = 0.0;       // 测量值，按 IEEE 754 位模式原样传输
    Quality quality = Quality::Good;
};