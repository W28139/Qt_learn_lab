#pragma once

#include "telemetrytypes.h"

#include <QHash>
#include <QList>

// 单个通道的累计统计。
// 均值用增量公式维护，避免保存所有样本：
//   mean += (value - mean) / count
struct ChannelStats
{
    qint64 count = 0;
    double mean = 0.0;
    double min = 0.0;
    double max = 0.0;
    qint64 good = 0;
    qint64 warning = 0;
    qint64 bad = 0;
};

// 纯逻辑模块：不依赖界面、数据库和网络，方便单元测试。
// 本 Lab 的测试目标就是它。
class SessionStats
{
public:
    void reset();
    void addSample(const TelemetrySample &sample);

    qint64 totalCount() const { return m_total; }
    bool isEmpty() const { return m_total == 0; }

    // 按通道号升序返回出现过的通道。
    QList<int> channels() const;

    // 通道不存在时返回 false，out 不改动。
    bool channelStats(int channel, ChannelStats *out) const;

private:
    QHash<int, ChannelStats> m_channels;
    qint64 m_total = 0;
};