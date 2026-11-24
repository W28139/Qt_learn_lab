#include "telemetrymodel.h"

#include <QBrush>
#include <QColor>
#include <QDateTime>

TelemetryModel::TelemetryModel(int capacity, QObject *parent)
    : QAbstractTableModel(parent)
    , m_ring(capacity)
{
}

int TelemetryModel::rowCount(const QModelIndex &parent) const
{
    // 表格模型没有子层级；parent 有效就说明视图在问“某个单元格下面有几行”。
    return parent.isValid() ? 0 : m_ring.size();
}

int TelemetryModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(ColumnCount);
}

QVariant TelemetryModel::data(const QModelIndex &index, int role) const
{
    // 防御性检查：无效索引、越界行一律返回空 QVariant，，避免视图/代理拿到脏数据。
    if (!index.isValid() || index.row() < 0 || index.row() >= m_ring.size())
        return {};

    // 逻辑行号 0 = 最旧样本，at() 内部做环形下标换算
    const TelemetrySample &sample = m_ring.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        // 展示文本：按列格式化。default 返回空，兼容未来新增列未实现的情况。
        switch (index.column()) {
        case SeqColumn:
            return QString::number(sample.seq);                 // 无符号序号，十进制
        case TimeColumn:
            // 时间戳（毫秒）转本地时间显示；只显示时分秒.毫秒，不显示日期。
            return QDateTime::fromMSecsSinceEpoch(sample.timestampMs)
                .toString(QStringLiteral("HH:mm:ss.zzz"));
        case ChannelColumn:
            // 通道号内部从 0 起，展示时 +1 变成人习惯的 CH1、CH2……
            return QStringLiteral("CH%1").arg(sample.channel + 1);
        case ValueColumn:
            return QString::number(sample.value, 'f', 3);       // 定点 3 位小数
        case QualityColumn:
            return qualityText(sample.quality);                 // "正常"/"警告"/"异常"
        default:
            return {};                                          // 未知列
        }

    case Qt::TextAlignmentRole:
        // 数值/时间右对齐便于比较列宽；文本类（通道、质量）左对齐。
        // 注意 Qt 要求返回 int 形式的 Alignment（QFlags 不能直接塞进 QVariant）。
        switch (index.column()) {
        case SeqColumn:
        case TimeColumn:
        case ValueColumn:
            return int(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        }

    case Qt::ForegroundRole:
        // 前景色按整行质量着色，与当前列无关：
        // Warning -> 棕橙色，Bad -> 红色，Good 走默认前景（返回空 QVariant）。
        switch (sample.quality) {
        case Quality::Warning:
            return QBrush(QColor(0xB0, 0x6A, 0x00));
        case Quality::Bad:
            return QBrush(QColor(0xC6, 0x28, 0x28));
        case Quality::Good:
            break;                                              // 落到下方 return {}
        }
        return {};

    // ---- 自定义 Role：给 delegate / 导出代码提供原始类型，避免从文本反解析 ----
    case SeqRole:
        return QVariant::fromValue(sample.seq);                 // quint64
    case TimestampRole:
        return QVariant::fromValue(sample.timestampMs);         // qint64
    case ChannelRole:
        // 这里返回 0 基通道号（不加 1），与内部存储保持一致；展示层才 +1。
        return int(sample.channel);
    case ValueRole:
        return sample.value;                                    // double，隐式转 QVariant
    case QualityRole:
        // 返回 int 而非 Quality 枚举：enum class 进 QVariant 需要额外注册元类型
        return int(sample.quality);

    default:
        return {};                                              // 未处理角色
    }
}

QVariant TelemetryModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const
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
        return QStringLiteral("状态");
    default:
        return {};
    }
}

Qt::ItemFlags TelemetryModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void TelemetryModel::appendSamples(const QList<TelemetrySample> &samples)
{
    const int total = int(samples.size());
    if (total <= 0)
        return;

    // 一批数据比整个缓冲区还大时，只保留最新的 capacity 条
    const int skip = qMax(0, total - m_ring.capacity());
    const int count = total - skip;
    if (count <= 0)
        return;

    const int overflow = qMax(0, m_ring.size() + count - m_ring.capacity());
    if (overflow > 0) {
        beginRemoveRows(QModelIndex(), 0, overflow - 1);
        for (int i = 0; i < overflow; ++i)
            m_ring.dropFront();
        endRemoveRows();
    }

    const int first = m_ring.size();
    beginInsertRows(QModelIndex(), first, first + count - 1);
    for (int i = 0; i < count; ++i)
        m_ring.pushBack(samples.at(skip + i));
    endInsertRows();

    m_totalReceived += quint64(count);
}

void TelemetryModel::clear()
{
    beginResetModel();
    m_ring.clear();
    m_totalReceived = 0;
    endResetModel();
}