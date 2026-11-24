#pragma once

#include "telemetrytypes.h"

#include <QList>
#include <QMutex>
#include <QString>
#include <QtGlobal>

// 工作线程 -> GUI 线程传递的一批样本。
// 纯数据结构、按值传递；不继承 QObject，也不持有任何 Qt 对象的指针。
struct TelemetryBatch
{
    quint32 sequence = 0;            // 帧头序号，仅用于日志对照，不参与排序
    QString deviceId;                // 设备标识，从载荷里解码得到
    QList<TelemetrySample> samples;  // 一批样本，GUI 侧会整体 append 进模型
};

// 有界、线程安全的批次队列。
//
// 生产者（worker 线程）调用 push()，消费者（GUI 线程）调用 takeAll()。
// 满了丢弃最旧的一批，而不是阻塞生产者：实时遥测里最新数据比完整历史重要，
// 也避免网络线程被 GUI 的刷新速度拖死。
//
// 数据和统计共用同一把锁：push/takeAll 更新队列的同时更新计数，
// 读统计也加锁，保证“深度、峰值、丢弃数”是同一时刻的一致快照。
class BoundedBatchQueue
{
public:
    explicit BoundedBatchQueue(qsizetype capacity);

    // 入队。队列满时先丢弃最旧的一批再入队，返回 false 表示发生了丢弃。
    bool push(const TelemetryBatch &batch);

    // 原子地取走当前所有批次（队列清空）。没有数据时返回空列表。
    QList<TelemetryBatch> takeAll();

    // 清空数据，但不重置 pushed/dropped/peak 等运行期统计：
    // 这些数字用于观察整次运行的压力，清表不应把它们抹掉。
    void clear();

    // 以下访问器内部加锁，可从任意线程调用。
    qsizetype size() const;               // 当前队列深度（批数）
    qsizetype capacity() const;           // 容量上限（构造时确定，加锁仅为接口一致性）
    qsizetype highWatermark() const;      // 历史最大深度，用于判断容量是否偏小
    quint64 pushedCount() const;          // 累计 push 成功次数（含后来被丢弃的）
    quint64 droppedCount() const;         // 累计因满被丢弃的批数
    quint64 poppedBatches() const;        // 累计被 takeAll 取走的批数
    quint64 poppedSamples() const;        // 累计被取走的样本数（用于算吞吐/延迟）

private:
    mutable QMutex m_mutex;         // mutable：const 访问器也要加锁
    QList<TelemetryBatch> m_items;  // 逻辑上第 0 个元素最旧，push 在尾部 append、满时 removeFirst，符合 FIFO
    const qsizetype m_capacity;     // 构造后不变，读取不需要加锁

    // 以下计数全部在同一把锁内更新，避免“队列已变、计数还没变”的撕裂窗口。
    quint64 m_pushed = 0;           // 累计入队批数
    quint64 m_dropped = 0;          // 累计丢弃批数
    quint64 m_poppedBatches = 0;    // 累计出队（takeAll 取走）的批数
    quint64 m_poppedSamples = 0;    // 累计出队的样本数（用于统计真实吞吐）
    qsizetype m_highWatermark = 0;  // 历史最大深度，用于判断容量是否过小
};