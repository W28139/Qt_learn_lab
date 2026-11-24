#pragma once

#include <QList>
#include <QtGlobal>

// 固定容量的环形缓冲：pushBack / dropFront 都是 O(1)，不会在运行期扩容。
// at(0) 是最旧的元素，at(size() - 1) 是最新的元素。
template <typename T>
class RingBuffer
{
public:
    explicit RingBuffer(int capacity)
        : m_data(qMax(1, capacity))     // capacity 容量，若 < 1 则按 1 处理
    {
    }

    int capacity() const { return int(m_data.size()); }
    int size() const { return m_size; } // 当前有效元素个数，范围 [0, capacity()]
    bool isEmpty() const { return m_size == 0; }

    void clear()
    {
        m_head = 0;
        m_size = 0;
    }

    void pushBack(const T &value)
    {
        m_data[m_head] = value;
        m_head = (m_head + 1) % capacity();
        if (m_size < capacity())
            ++m_size;
    }

    // 丢弃最旧的一个元素。槽位里的旧值不用清空，at() 会忽略它。
    void dropFront()
    {
        if (m_size > 0)
            --m_size;
    }

    const T &at(int index) const
    {
        Q_ASSERT(index >= 0 && index < m_size);
        const int cap = capacity();
        return m_data[(m_head - m_size + index + cap) % cap];
    }

private:
    QList<T> m_data;    // 固定长度的槽位数组，长度即 capacity()
    int m_head = 0;   // 下一个写入位置
    int m_size = 0;   // 当前有效元素个数
};