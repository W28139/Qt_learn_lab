#pragma once

#include "ringbuffer.h"     // RingBuffer<T>：定长环形缓冲，用于每个通道的样本存储
#include "telemetrytypes.h"

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QWidget>
#include <QtGlobal>

#include <memory>
#include <vector>

class QPainter;

// 实时趋势图
// 只做一件事：把 GUI 线程收到的样本画成多条滑动窗口曲线。
// 所有绘制都在 paintEvent() 里完成；appendSamples() 只改内存数据并请求一次 update()，不直接画图。

class ChartWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit ChartWidget(QWidget *parent = nullptr);
    ~ChartWidget() override;

    // 由 GUI 线程调用：追加一批样本，并按需请求重绘。
    // 时间戳按服务器时间解释，窗口右边界跟随最新样本。
    // 这里只更新 m_newestMs 与各 Series 的环形缓冲，然后调用一次 update()，不在此处做任何 QPainter 绘制
    void appendSamples(const QList<TelemetrySample> &samples);
    // 清空所有曲线数据
    void clearData();

    // 可见时间窗口长度（秒）。默认 10 秒。
    void setWindowSeconds(double seconds);
    // 内部以毫秒存储（m_windowMs），对外以秒暴露，避免调用方处理毫秒单位。
    double windowSeconds() const { return double(m_windowMs) / 1000.0; }

    // 暂停只冻结横轴右边界，样本仍会继续写入环形缓冲；
    // 恢复后直接跳到最新数据。
    void setPaused(bool paused);
    bool isPaused() const { return m_paused; }

    // 自动缩放 Y 轴。开启时每次 paintEvent 按当前可见数据重算 Y 范围；
    // 关闭时沿用上一次计算出的 m_yMin/m_yMax/m_yStep。
    void setAutoScale(bool enabled);
    bool autoScale() const { return m_autoScale; }

    // 是否绘制网格与刻度
    void setShowGrid(bool enabled);
    bool showGrid() const { return m_showGrid; }

    // 用于状态栏的可观测性指标。
    // paintCount    ：累计 paintEvent 次数（可判断是否被频繁触发）；
    // lastPaintMs   ：最近一次 paintEvent 的耗时（毫秒）；
    // paintedPointCount：最近一次实际绘制的点数（评估绘制负载）。
    quint64 paintCount() const { return m_paintCount; }
    double lastPaintMs() const { return m_lastPaintMs; }
    int paintedPointCount() const { return m_paintedPoints; }

protected:
    // 所有绘制入口
    void paintEvent(QPaintEvent *event) override;

private:
    // 单个采样点在时间-数值平面上的表示。
    struct SamplePoint
    {
        qint64 timestampMs = 0;
        double value = 0.0;
    };

    // 一条曲线 = 一个通道。拥有自己的 RingBuffer，容量在构造时固定。
    struct Series
    {
        quint16 channel = 0;   // 通道号，作为 Series 的唯一键
        QString name;          // 显示名称（图例用）
        QColor color;          // 曲线颜色（创建 Series 时分配）
        RingBuffer<SamplePoint> points;  // 该通道的样本环形缓冲

        explicit Series(int capacity) : points(capacity){}
    };

    // 按通道号查找 Series 曲线；不存在则创建一个
    Series *seriesForChannel(quint16 channel);
    // 当前视图右边界：
    //   - 未暂停：返回 m_newestMs（跟随最新样本）；
    //   - 暂停：  返回 m_frozenEndMs（冻结在暂停时刻）。
    qint64 effectiveEndMs() const;

    // 在 [startMs, endMs] 可见范围内扫描所有 Series 的点
    void computeYRange(qint64 startMs, qint64 endMs);

    // 以下为绘制子步骤，均只在 paintEvent() 内调用。
    void drawTitle(QPainter &painter);
    void drawEmptyState(QPainter &painter, const QRectF &plot);
    void drawGrid(QPainter &painter, const QRectF &plot, qint64 startMs, qint64 endMs);
    void drawSeries(QPainter &painter, const QRectF &plot, qint64 startMs, qint64 endMs);
    void drawLegend(QPainter &painter, const QRectF &plot);
    void drawPausedBadge(QPainter &painter, const QRectF &plot);

    // 把 (timestampMs, value) 映射到 plot 矩形内的屏幕坐标
    QPointF mapPoint(const SamplePoint &point, const QRectF &plot, qint64 startMs,
                     qint64 endMs) const;

    // 所有通道的曲线集合
    std::vector<std::unique_ptr<Series>> m_series;

    qint64 m_newestMs = 0;      // 收到的最新样本时间（窗口右边界）
    qint64 m_windowMs = 10000;  // 当前可见时间窗口
    qint64 m_frozenEndMs = 0;   // 暂停时冻结的右边界
    bool m_paused = false;
    bool m_autoScale = true;
    bool m_showGrid = true;

    // 当前 Y 轴范围。自动缩放时在 paintEvent 里按可见数据重新计算。
    // m_yStep 是计算范围时采用的 nice step，画刻度时复用，保证网格与范围一致。
    double m_yMin = 0.0;
    double m_yMax = 100.0;
    double m_yStep = 20.0;

    // 可观测性指标，供状态栏/调试面板读取；不参与绘制决策。
    quint64 m_paintCount = 0;
    double m_lastPaintMs = 0.0;
    int m_paintedPoints = 0;

    // 复用的屏幕坐标缓冲：避免每次重绘都重新分配大块内存。
    std::vector<QPointF> m_screenPoints;
};