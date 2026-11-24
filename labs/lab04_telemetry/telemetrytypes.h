#pragma once

#include <QString>
#include <QtGlobal>

// 测数据点的质量等级
enum class Quality : quint8 {
    Good = 0,
    Warning = 1,
    Bad = 2
};

// 单条遥测样本
struct TelemetrySample
{
    qint64  timestampMs = 0;                 // 采样时间戳
    quint64 seq         = 0;                 // 序号，用于检测丢包/乱序；通常单调递增。
    quint16 channel     = 0;                 // 通道号（0..65535），标识数据来源通道/测点。
    double  value       = 0.0;               // 采样值（物理量，单位由通道定义，本结构不携带单位信息）。
    Quality quality     = Quality::Good;     // 质量等级，默认正常。
};

// 把质量等级转成面向用户的中文描述
inline QString qualityText(Quality quality)
{
    switch (quality) {
    case Quality::Good:
        return QStringLiteral("正常");
    case Quality::Warning:
        return QStringLiteral("警告");
    case Quality::Bad:
        return QStringLiteral("异常");
    }
    return QStringLiteral("未知");
}