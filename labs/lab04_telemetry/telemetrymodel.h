#pragma once

#include <QAbstractTableModel>
#include <QList>

#include "ringbuffer.h"
#include "telemetrytypes.h"

// 遥测数据表格模型
class TelemetryModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    // 列索引(顺序即表头的展示顺序)
    enum Column {
        SeqColumn = 0,   // 序号列
        TimeColumn,      // 时间戳列
        ChannelColumn,   // 通道号列
        ValueColumn,     // 采样值列
        QualityColumn,   // 质量等级列
        ColumnCount      // 列总数
    };

    enum Role {
        SeqRole = Qt::UserRole + 1,  // 序号原始值
        TimestampRole,               // 时间戳原始值（毫秒）
        ChannelRole,                 // 通道号原始值
        ValueRole,                   // 采样值原始值
        QualityRole                  // 质量等级原始值（可用于 delegate 着色）
    };

    // 构造函数
    explicit TelemetryModel(int capacity, QObject *parent = nullptr);


    /// 行数 = 当前缓冲中的有效样本数（parent 有效时返回 0）。
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    /// 列数 = ColumnCount（parent 有效时返回 0）。
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    // 按角色返回单元格数据
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;

    // 表头文本
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    // 返回单元格标志。只读模型：仅启用/可选，不含 ItemIsEditable。
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // ---- 数据写入（供采集/解析线程在 UI 线程调用） ----
    void appendSamples(const QList<TelemetrySample> &samples);

    // 清空模型内容（同时发出 reset 通知），totalReceived 是否归零由实现决定。
    void clear();

    // ---- 只读访问器 ----

    // 环形缓冲容量（= 最大可见行数）。
    int capacity() const { return m_ring.capacity(); }

    // 当前缓冲中的有效样本数（= rowCount()）。
    int bufferedCount() const { return m_ring.size(); }

    // 自模型创建/上次 clear 以来累计接收到的样本总数（含已被挤出缓冲的）。
    quint64 totalReceived() const { return m_totalReceived; }

private:
    RingBuffer<TelemetrySample> m_ring;  // 固定容量的样本存储，逻辑上第 0 行最旧。
    quint64 m_totalReceived = 0;         // 累计接收计数（含已淘汰的样本），用于统计/监控。
};