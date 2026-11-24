#include "boundedqueue.h"

#include <QMutexLocker>

#include <utility>

BoundedBatchQueue::BoundedBatchQueue(qsizetype capacity)
    : m_capacity(qMax<qsizetype>(1, capacity)) // 至少为 1，避免容量 0 的退化行为
{
}

bool BoundedBatchQueue::push(const TelemetryBatch &batch)
{
    QMutexLocker locker(&m_mutex);

    // 每次只 append 一个，正常最多删一批；while 是防御性写法。
    // 丢弃、入队、峰值和计数在同一临界区完成，保证读数一致。
    bool keptEverything = true;
    while (m_items.size() >= m_capacity) {
        m_items.removeFirst();   // 丢弃队头（最旧）的一条数据
        ++m_dropped;             // 累计丢弃计数
        keptEverything = false;  // 标记本次 push 发生了丢弃
    }

    m_items.append(batch);       // 尾部追加新数据（此时必有空位）
    ++m_pushed;                  //

    // 更新历史最高水位：用于监控队列实际被撑到过多大
    m_highWatermark = qMax(m_highWatermark, m_items.size());
    return keptEverything;
}

QList<TelemetryBatch> BoundedBatchQueue::takeAll()
{
    QList<TelemetryBatch> out;
    {
        QMutexLocker locker(&m_mutex);
        // swap 是 O(1)：锁内只交换两个 QList 的内部指针，不逐项拷贝；
        // 统计也在锁内更新，保证返回时 popped 计数已包含这批数据。
        out.swap(m_items);
        m_poppedBatches += static_cast<quint64>(out.size());
        for (const TelemetryBatch &batch : std::as_const(out))
            m_poppedSamples += static_cast<quint64>(batch.samples.size());
    }
    return out;
}

void BoundedBatchQueue::clear()
{
    QMutexLocker locker(&m_mutex);
    // 只清数据，不重置累计计数和峰值：这些数字反映整次运行的压力。
    m_items.clear();
}

qsizetype BoundedBatchQueue::size() const
{
    QMutexLocker locker(&m_mutex);
    return m_items.size();
}

qsizetype BoundedBatchQueue::capacity() const
{
    return m_capacity;
}

qsizetype BoundedBatchQueue::highWatermark() const
{
    QMutexLocker locker(&m_mutex);
    return m_highWatermark;
}

quint64 BoundedBatchQueue::pushedCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_pushed;
}

quint64 BoundedBatchQueue::droppedCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_dropped;
}

quint64 BoundedBatchQueue::poppedBatches() const
{
    QMutexLocker locker(&m_mutex);
    return m_poppedBatches;
}

quint64 BoundedBatchQueue::poppedSamples() const
{
    QMutexLocker locker(&m_mutex);
    return m_poppedSamples;
}