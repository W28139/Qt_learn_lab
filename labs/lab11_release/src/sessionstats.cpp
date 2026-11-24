#include "sessionstats.h"

#include <algorithm>

void SessionStats::reset()
{
    m_channels.clear();
    m_total = 0;
}

void SessionStats::addSample(const TelemetrySample &sample)
{
    ChannelStats &stats = m_channels[sample.channel];

    if (stats.count == 0) {
        stats.min = sample.value;
        stats.max = sample.value;
        stats.mean = sample.value;
    }
    else {
        stats.min = std::min(stats.min, sample.value);
        stats.max = std::max(stats.max, sample.value);
        // count 还是“旧样本数”，所以新均值除以 count + 1。
        stats.mean += (sample.value - stats.mean) / static_cast<double>(stats.count + 1);
    }

    ++stats.count;
    ++m_total;

    switch (sample.quality) {
    case Quality::Good:
        ++stats.good;
        break;
    case Quality::Warning:
        ++stats.warning;
        break;
    case Quality::Bad:
        ++stats.bad;
        break;
    }
}

QList<int> SessionStats::channels() const
{
    QList<int> result = m_channels.keys();
    std::sort(result.begin(), result.end());
    return result;
}

bool SessionStats::channelStats(int channel, ChannelStats *out) const
{
    const auto it = m_channels.constFind(channel);
    if (it == m_channels.constEnd())
        return false;

    if (out)
        *out = it.value();
    return true;
}