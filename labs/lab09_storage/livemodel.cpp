#include "livemodel.h"

#include <QDateTime>

LiveSampleModel::LiveSampleModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int LiveSampleModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int LiveSampleModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant LiveSampleModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const TelemetrySample &sample = m_rows.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case SeqColumn:
            return QVariant::fromValue(sample.seq);
        case TimeColumn:
            return QDateTime::fromMSecsSinceEpoch(sample.timestampMs)
                .toString(QStringLiteral("HH:mm:ss.zzz"));
        case ChannelColumn:
            return QStringLiteral("CH %1").arg(sample.channel);
        case ValueColumn:
            return QString::number(sample.value, 'f', 2);
        case QualityColumn:
            return qualityText(sample.quality);
        default:
            break;
        }
    } else if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case SeqColumn:
        case ValueColumn:
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return static_cast<int>(Qt::AlignCenter);
        }
    }

    return {};
}

QVariant LiveSampleModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};

    switch (section) {
    case SeqColumn:
        return QStringLiteral("序号");
    case TimeColumn:
        return QStringLiteral("时间");
    case ChannelColumn:
        return QStringLiteral("通道");
    case ValueColumn:
        return QStringLiteral("数值");
    case QualityColumn:
        return QStringLiteral("质量");
    default:
        return {};
    }
}

void LiveSampleModel::appendSamples(const QList<TelemetrySample> &samples)
{
    if (samples.isEmpty())
        return;

    // 极端的整批覆盖：直接重置比拆成“删光再插入”简单，且很少发生。
    if (samples.size() >= kCapacity) {
        beginResetModel();
        m_rows = samples.mid(samples.size() - kCapacity);
        endResetModel();
        return;
    }

    const qsizetype overflow = m_rows.size() + samples.size() - kCapacity;
    if (overflow > 0) {
        const qsizetype removeCount = qMin(overflow, m_rows.size());
        beginRemoveRows(QModelIndex(), 0, static_cast<int>(removeCount - 1));
        m_rows.remove(0, removeCount);
        endRemoveRows();
    }

    const int first = static_cast<int>(m_rows.size());
    const int last = first + static_cast<int>(samples.size()) - 1;
    beginInsertRows(QModelIndex(), first, last);
    m_rows.append(samples);
    endInsertRows();
}

void LiveSampleModel::clear()
{
    if (m_rows.isEmpty())
        return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
}