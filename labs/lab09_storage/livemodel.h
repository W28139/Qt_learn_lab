#pragma once

#include "telemetrytypes.h"

#include <QAbstractTableModel>
#include <QList>

// 实时样本表：只保留最近 N 条，避免界面模型无限增长。
// 这是 GUI 侧的“观察窗口”，不是数据库的缓存——
// 落盘的是全部样本，这里丢掉的旧行仍然在 SQLite 里可查。
class LiveSampleModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        SeqColumn = 0,
        TimeColumn,
        ChannelColumn,
        ValueColumn,
        QualityColumn,
        ColumnCount,
    };

    explicit LiveSampleModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void appendSamples(const QList<TelemetrySample> &samples);
    void clear();

private:
    static constexpr qsizetype kCapacity = 200;
    QList<TelemetrySample> m_rows;
};