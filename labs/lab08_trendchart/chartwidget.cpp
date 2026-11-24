#include "chartwidget.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPen>

#include <algorithm>
#include <climits>
#include <cmath>
#include <utility>

namespace {

// 绘图区四边留白（像素）：左留 Y 刻度、上留标题/图例、右少量、下留时间刻度。
constexpr int kMarginLeft = 72;
constexpr int kMarginTop = 54;
constexpr int kMarginRight = 20;
constexpr int kMarginBottom = 44;
// 每个通道的环形缓冲容量。20000 点足以覆盖 60 秒窗口（按常见采样率估算）。
constexpr int kSeriesCapacity = 20000;
// 最多同时显示的通道数，超过则丢弃新通道（避免颜色耗尽/图例溢出）。
constexpr std::size_t kMaxChannels = 8;
// Y 轴期望的分段数量，用于把范围切成 5 格。
constexpr int kYIntervals = 5;

// 8 种区分度较高的曲线颜色，按通道创建顺序依次分配。
const QColor kSeriesColors[] = {
    QColor(0x4E, 0x79, 0xA7), QColor(0xF2, 0x8E, 0x2B), QColor(0x59, 0xA1, 0x4F),
    QColor(0xE1, 0x57, 0x59), QColor(0x76, 0xB7, 0xB2), QColor(0xB0, 0x7A, 0xA1),
    QColor(0xFF, 0x9D, 0xA4), QColor(0x9C, 0x75, 0x5F),
};

// 把原始步长向上取整到 1/2/5×10^n，得到“人类友好”的刻度间隔。
// 例如 0.37 -> 0.5，2.4 -> 5，47 -> 50，避免出现 3.47 这类刻度。
double niceStep(double rawStep)
{
    // 非有限值或非正值时退化为 1.0，防止后续除零/NaN 传播。
    if (!(rawStep > 0.0) || !std::isfinite(rawStep))
        return 1.0;

    // 取量级（10 的整数次幂），再按尾数向上取整到 1/2/5/10。
    const double exponent = std::floor(std::log10(rawStep));
    const double base = std::pow(10.0, exponent);
    const double fraction = rawStep / base;

    double nice = 10.0;            // 默认向上取到 10
    if (fraction <= 1.0)
        nice = 1.0;
    else if (fraction <= 2.0)
        nice = 2.0;
    else if (fraction <= 5.0)
        nice = 5.0;

    return nice * base;
}

// 根据步长决定刻度文字的小数位数：步长越小，保留位越多。
int decimalsForStep(double step)
{
    if (step >= 1.0)
        return 0;
    if (step >= 0.1)
        return 1;
    if (step >= 0.01)
        return 2;
    if (step >= 0.001)
        return 3;
    return 4;
}

// 毫秒时间戳 -> "HH:mm:ss"，用于 X 轴刻度。
QString formatTickTime(qint64 timestampMs)
{
    return QDateTime::fromMSecsSinceEpoch(timestampMs).toString(QStringLiteral("HH:mm:ss"));
}

} // namespace

ChartWidget::ChartWidget(QWidget *parent)
    : QWidget(parent)
{
    // 给一个下限尺寸，避免被压缩到无法绘制。
    setMinimumSize(480, 280);
    // 随父布局自由伸展。
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // paintEvent 会填满整个背景，声明不透明可省去 Qt 的背景擦除。
    setAttribute(Qt::WA_OpaquePaintEvent);
}

// Series 内含 RingBuffer（非平凡类型），析构放在 cpp 里以见到完整定义。
ChartWidget::~ChartWidget() = default;

void ChartWidget::appendSamples(const QList<TelemetrySample> &samples)
{
    if (samples.isEmpty())
        return;

    bool appended = false;
    for (const TelemetrySample &sample : samples) {
        // NaN/Inf 会让 mapPoint 与自动缩放产生无效值，直接丢弃。
        if (!std::isfinite(sample.value))
            continue;

        // 查找/懒创建该通道对应的曲线。
        Series *series = seriesForChannel(sample.channel);
        // 通道数已达上限时返回 nullptr，丢弃多余通道的数据。
        if (!series)
            continue;

        // 写入环形缓冲；满时自动覆盖最旧点。
        series->points.pushBack(SamplePoint{sample.timestampMs, sample.value});
        // 跟踪最新时间戳，作为未暂停时的窗口右边界。
        if (sample.timestampMs > m_newestMs)
            m_newestMs = sample.timestampMs;
        appended = true;
    }

    // 暂停时依然收数据，只是不请求重绘；恢复后立即跳到最新窗口。
    if (appended && !m_paused)
        update();
}

void ChartWidget::clearData()
{
    // 只清点，不清 Series 本身（保留通道号/名称/颜色，避免重建）
    for (const auto &series : m_series)
        series->points.clear();
    m_newestMs = 0;
    m_frozenEndMs = 0;
    m_paintedPoints = 0;
    update();
}

// 设置横轴可见的时间跨度（单位：秒）
void ChartWidget::setWindowSeconds(double seconds)
{
    // 限制在 2~120 秒，防止极端值导致绘制或布局异常。
    const double clamped = std::clamp(seconds, 2.0, 120.0);
    const qint64 windowMs = qRound64(clamped * 1000.0);
    // 无变化时不触发重绘，减少无效 paintEvent。
    if (windowMs == m_windowMs)
        return;

    m_windowMs = windowMs;
    update();
}

// 暂停/恢复曲线滚动
void ChartWidget::setPaused(bool paused)
{
    if (m_paused == paused)
        return;

    m_paused = paused;
    if (m_paused) {
        // 进入暂停：把右边界冻结在“当前最新样本”处。
        m_frozenEndMs = m_newestMs;
    } else {
        // 恢复：清掉冻结值，effectiveEndMs() 将重新跟随最新样本。
        m_frozenEndMs = 0;
    }

    update();
}

// Y 轴自动缩放开关
void ChartWidget::setAutoScale(bool enabled)
{
    if (m_autoScale == enabled)
        return;

    m_autoScale = enabled;
    update();
}

// 网格线开关
void ChartWidget::setShowGrid(bool enabled)
{
    if (m_showGrid == enabled)
        return;

    m_showGrid = enabled;
    update();
}

// 按通道号查找已有 Series；找不到则在未超上限时新建。
ChartWidget::Series *ChartWidget::seriesForChannel(quint16 channel)
{
    for (const auto &series : m_series) {
        if (series->channel == channel)
            return series.get();
    }

    // 超过上限就不再分配新曲线，直接返回 nullptr。
    if (m_series.size() >= kMaxChannels)
        return nullptr;

    auto series = std::make_unique<Series>(kSeriesCapacity);
    series->channel = channel;
    // 显示名用通道号；后续若需要可在此处改为真实名称。
    series->name = QStringLiteral("通道 %1").arg(channel);
    // 颜色按顺序循环分配，保证相邻通道颜色不同。
    series->color = kSeriesColors[m_series.size() % std::size(kSeriesColors)];
    m_series.push_back(std::move(series));
    return m_series.back().get();
}

// 视图右边界：暂停用冻结值，否则跟随最新样本。
qint64 ChartWidget::effectiveEndMs() const
{
    if (m_paused)
        return m_frozenEndMs > 0 ? m_frozenEndMs : m_newestMs;
    return m_newestMs;
}

// 时间/数值 -> 像素坐标的线性映射。
// 注意：不处理越界，调用方需保证 point 在窗口内，或由 clipRect 兜底。
QPointF ChartWidget::mapPoint(const SamplePoint &point, const QRectF &plot, qint64 startMs,
                              qint64 endMs) const
{
    // 时间方向：左 = startMs，右 = endMs。
    const double xRatio = double(point.timestampMs - startMs) / double(endMs - startMs);
    // 数值方向：下 = m_yMin，上 = m_yMax。
    const double yRatio = (point.value - m_yMin) / (m_yMax - m_yMin);
    return QPointF(plot.left() + xRatio * plot.width(),
                   plot.bottom() - yRatio * plot.height());
}

void ChartWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    // 计时器统计本次 paintEvent 总耗时，写入 m_lastPaintMs。
    QElapsedTimer timer;
    timer.start();

    QPainter painter(this);
    // 曲线/圆点用抗锯齿保证平滑；文字也用抗锯齿。
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    // 铺满背景（配合 WA_OpaquePaintEvent，避免闪烁/重影）。
    const QPalette pal = palette();
    painter.fillRect(rect(), pal.color(QPalette::Base));

    drawTitle(painter);

    // 从整体矩形扣掉四边留白，得到实际绘图区。
    const QRectF plot = QRectF(rect()).adjusted(kMarginLeft, kMarginTop, -kMarginRight,
                                                -kMarginBottom);
    // 绘图区太小就不画内容，只记录耗时，避免除零/布局错乱。
    if (plot.width() < 60.0 || plot.height() < 40.0) {
        m_lastPaintMs = timer.nsecsElapsed() / 1.0e6;
        ++m_paintCount;
        return;
    }

    const qint64 endMs = effectiveEndMs();          // 右边界（暂停/跟随）
    const qint64 startMs = endMs - m_windowMs;      // 左边界 = 右边界 - 窗口长度
    // 还没有任何样本：画空状态提示 + 暂停角标（如有）。
    if (m_newestMs <= 0 || endMs <= 0) {
        drawEmptyState(painter, plot);
        drawPausedBadge(painter, plot);
        m_lastPaintMs = timer.nsecsElapsed() / 1.0e6;
        ++m_paintCount;
        return;
    }

    // 正常绘制顺序：Y 范围 -> 网格/轴 -> 曲线 -> 图例 -> 暂停角标。
    computeYRange(startMs, endMs);
    drawGrid(painter, plot, startMs, endMs);
    drawSeries(painter, plot, startMs, endMs);
    drawLegend(painter, plot);
    drawPausedBadge(painter, plot);

    // 统计本次耗时和次数，供状态栏读取。
    m_lastPaintMs = timer.nsecsElapsed() / 1.0e6;
    ++m_paintCount;
}

// 根据“当前可见时间窗口内的数据”重新算出 m_yMin / m_yMax / m_yStep 三个成员
void ChartWidget::computeYRange(qint64 startMs, qint64 endMs)
{
    // 固定范围模式：给“已知量程”场景使用，固定 0~100。
    if (!m_autoScale) {
        m_yMin = 0.0;
        m_yMax = 100.0;
        m_yStep = niceStep((m_yMax - m_yMin) / kYIntervals);
        return;
    }

    // 自动模式：扫描窗口内所有通道，求数据 min/max。
    bool hasData = false;
    double dataMin = 0.0;
    double dataMax = 0.0;

    for (const auto &series : m_series) {
        const int n = series->points.size();
        for (int i = 0; i < n; ++i) {
            const SamplePoint &point = series->points.at(i);
            // 只看当前时间窗口内的点。
            if (point.timestampMs < startMs || point.timestampMs > endMs)
                continue;

            if (!hasData) {
                dataMin = dataMax = point.value;
                hasData = true;
            } else {
                dataMin = std::min(dataMin, point.value);
                dataMax = std::max(dataMax, point.value);
            }
        }
    }

    // 窗口内没有数据：保留上一帧范围，避免 Y 轴来回跳。
    if (!hasData)
        return;

    // 所有值相等：人为扩出 ±1，避免 yRatio 除零。
    if (dataMax - dataMin < 1.0e-9) {
        dataMin -= 1.0;
        dataMax += 1.0;
    }

    // 上下各留 8% + 0.5 的余量，防止曲线贴边。
    const double padding = (dataMax - dataMin) * 0.08 + 0.5;
    const double low = dataMin - padding;
    const double high = dataMax + padding;
    // 用 nice step 把范围切成 5 格左右，便于阅读。
    m_yStep = niceStep((high - low) / kYIntervals);

    // 把上下界对齐到步长的整数倍，使刻度落点整齐。
    m_yMin = std::floor(low / m_yStep) * m_yStep;
    m_yMax = std::ceil(high / m_yStep) * m_yStep;
    // 兜底：保证范围有效。
    if (m_yMax <= m_yMin)
        m_yMax = m_yMin + m_yStep;
}

void ChartWidget::drawTitle(QPainter &painter)
{
    // 标题字体稍大并加粗。
    QFont titleFont = font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(font().pointSizeF() + 1.0);
    painter.setFont(titleFont);

    // 使用主题文字色，自动适配深浅色主题。
    QColor textColor = palette().color(QPalette::Text);
    painter.setPen(textColor);
    // 左上角一行标题，显示当前窗口秒数。
    painter.drawText(QRectF(20.0, 10.0, width() - 40.0, 28.0),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("实时趋势 · 最近 %1 秒").arg(windowSeconds(), 0, 'g', 3));
}

// 无数据时的占位提示：一行主提示 + 一行更淡的副提示。
void ChartWidget::drawEmptyState(QPainter &painter, const QRectF &plot)
{
    QColor textColor = palette().color(QPalette::Text);

    // 主提示：稍大号，半透明。
    QFont emptyFont = font();
    emptyFont.setPointSizeF(font().pointSizeF() + 2.0);
    painter.setFont(emptyFont);
    textColor.setAlpha(160);
    painter.setPen(textColor);
    painter.drawText(plot, Qt::AlignCenter, QStringLiteral("等待遥测数据…"));

    // 副提示：更淡，向下偏移 30px，避免与主提示重叠。
    QFont hintFont = font();
    textColor.setAlpha(110);
    painter.setFont(hintFont);
    painter.drawText(plot.adjusted(0.0, 30.0, 0.0, 0.0),
                     Qt::AlignHCenter | Qt::AlignTop,
                     QStringLiteral("worker 启动后约 1 秒会出现曲线"));
}

// 网格 / 坐标轴 / 刻度：用同一个 Text 颜色派生出不同透明度。
void ChartWidget::drawGrid(QPainter &painter, const QRectF &plot, qint64 startMs, qint64 endMs)
{
    const QColor textColor = palette().color(QPalette::Text);
    QColor muted = textColor;       // 刻度文字颜色
    muted.setAlpha(170);
    QColor gridColor = textColor;   // 网格线颜色（很淡）
    gridColor.setAlpha(28);
    QColor axisColor = textColor;   // 坐标轴线颜色（较实）
    axisColor.setAlpha(90);

    // 刻度文字用略小的字体，避免拥挤。
    QFont labelFont = font();
    labelFont.setPointSizeF(std::max(7.5, font().pointSizeF() - 1.0));
    painter.setFont(labelFont);

    // Y 方向：使用 computeYRange 已算好的 m_yStep，保证网格与范围一致。
    const double yRange = m_yMax - m_yMin;
    const double yStep = m_yStep > 0.0 ? m_yStep : niceStep(yRange / kYIntervals);
    const int yDecimals = decimalsForStep(yStep);

    // 第一条横线的位置：把 m_yMin 向上取整到 step 的整数倍。
    const double firstY = std::ceil(m_yMin / yStep) * yStep;
    for (double value = firstY; value <= m_yMax + yStep * 1.0e-6; value += yStep) {
        // 数值 -> 像素 y。
        const qreal y = plot.bottom() - (value - m_yMin) / yRange * plot.height();
        if (m_showGrid) {
            painter.setPen(QPen(gridColor, 1.0));
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }

        // 避免显示 "-0"。
        double labelValue = value;
        if (std::abs(labelValue) < yStep * 1.0e-6)
            labelValue = 0.0;
        // 左对齐到绘图区左边的留白处，右对齐显示。
        painter.setPen(muted);
        painter.drawText(QRectF(0.0, y - 9.0, plot.left() - 10.0, 18.0),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(labelValue, 'f', yDecimals));
    }

    // 坐标轴本体：底边 + 左边。
    painter.setPen(QPen(axisColor, 1.0));
    painter.drawLine(plot.bottomLeft(), plot.bottomRight());
    painter.drawLine(plot.topLeft(), plot.bottomLeft());

    // X 方向：期望 5 格，步长向上取到“整秒”级别。
    const double xStepSec = std::max(0.5, niceStep(windowSeconds() / 5.0));
    const qint64 xStepMs = std::max<qint64>(100, qRound64(xStepSec * 1000.0));
    // 第一个刻度：对齐到 xStepMs 的整数倍（从 startMs 向前推进）。
    const qint64 firstTick = startMs + ((xStepMs - (startMs % xStepMs)) % xStepMs);

    for (qint64 tick = firstTick; tick <= endMs; tick += xStepMs) {
        // 时间 -> 像素 x。
        const qreal x = plot.left()
                        + double(tick - startMs) / double(m_windowMs) * plot.width();
        if (m_showGrid) {
            painter.setPen(QPen(gridColor, 1.0));
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }

        // 刻度文字：以 x 为中心，画在底边下方 6px 处。
        painter.setPen(muted);
        painter.drawText(QRectF(x - 55.0, plot.bottom() + 6.0, 110.0, 22.0),
                         Qt::AlignHCenter | Qt::AlignTop, formatTickTime(tick));
    }
}

void ChartWidget::drawSeries(QPainter &painter, const QRectF &plot, qint64 startMs, qint64 endMs)
{
    m_paintedPoints = 0;

    painter.save();
    // 曲线超出绘图区的部分裁掉（窗口外的极端点也不会溢出）。
    painter.setClipRect(plot);

    for (const auto &seriesPtr : m_series) {
        const Series &series = *seriesPtr;
        const int n = series.points.size();
        if (n == 0)
            continue;

        // 环形缓冲里时间单调递增：找到窗口内第一/最后一个点的下标。
        int first = 0;
        while (first < n && series.points.at(first).timestampMs < startMs)
            ++first;

        int last = n - 1;
        while (last >= first && series.points.at(last).timestampMs > endMs)
            --last;

        // 整条曲线都不在窗口内。
        if (first > last)
            continue;

        const int visibleCount = last - first + 1;
        m_paintedPoints += visibleCount;   // 统计绘制点数，供状态栏展示

        // 用像素列数作为是否降采样的判据。
        const int columnCount = std::max(1, int(plot.width()));
        m_screenPoints.clear();
        // 预分配：直接绘制最多 visibleCount 点；降采样每列最多 4 点。
        const std::size_t reserveCount = visibleCount <= columnCount
                                             ? static_cast<std::size_t>(visibleCount)
                                             : static_cast<std::size_t>(columnCount) * 4;
        m_screenPoints.reserve(reserveCount);

        if (visibleCount <= columnCount) {
            // 点数比像素列少：每个点都映射，保留原始细节。
            for (int i = first; i <= last; ++i)
                m_screenPoints.push_back(mapPoint(series.points.at(i), plot, startMs, endMs));
        } else {
            // 点数比像素列多：按列压成 min/max，保住尖峰，同时限制线段数。
            int column = INT_MIN;    // 当前列号（初始化为不可能值）
            int colFirst = -1;       // 当前列第一个点的下标
            int colLast = -1;        // 当前列最后一个点的下标
            int colMin = -1;         // 当前列最小值点下标
            int colMax = -1;         // 当前列最大值点下标

            // 输出当前列的代表点（首/最小/最大/尾，排序去重后按时间顺序输出）。
            auto flushColumn = [&] {
                if (column == INT_MIN)
                    return;

                int indices[4] = {colFirst, colMin, colMax, colLast};
                std::sort(std::begin(indices), std::end(indices));
                int previous = -1;
                for (int index : indices) {
                    if (index < 0 || index == previous)   // 跳过重复/未初始化
                        continue;
                    previous = index;
                    m_screenPoints.push_back(
                        mapPoint(series.points.at(index), plot, startMs, endMs));
                }
            };

            for (int i = first; i <= last; ++i) {
                const SamplePoint &point = series.points.at(i);
                // 用 floor(x) 作为列号，保证同一像素列的点被归到一起。
                const qreal x = plot.left()
                                + double(point.timestampMs - startMs) / double(m_windowMs)
                                      * plot.width();
                const int currentColumn = int(std::floor(x));

                if (currentColumn != column) {
                    // 进入新列：先把上一列的代表点输出。
                    flushColumn();
                    column = currentColumn;
                    colFirst = colLast = colMin = colMax = i;
                } else {
                    // 同一列：更新尾点、最小点、最大点。
                    colLast = i;
                    if (point.value < series.points.at(colMin).value)
                        colMin = i;
                    if (point.value > series.points.at(colMax).value)
                        colMax = i;
                }
            }
            // 处理最后一列。
            flushColumn();
        }

        // 至少 2 个点才画折线；单点由下方的标记点表示。
        painter.setBrush(Qt::NoBrush);
        if (m_screenPoints.size() >= 2) {
            QPen pen(series.color);
            pen.setWidthF(2.0);
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            painter.drawPolyline(m_screenPoints.data(), int(m_screenPoints.size()));
        }

        // 最新点标记：一眼看出数据是否还在流动。
        const QPointF marker =
            mapPoint(series.points.at(last), plot, startMs, endMs);
        painter.setPen(QPen(series.color, 1.5));
        painter.setBrush(palette().color(QPalette::Base));  // 内部填背景色，形成“空心圆”
        painter.drawEllipse(marker, 3.0, 3.0);
    }

    painter.restore();
}

// 顶部右侧图例：颜色短线 + “名称  最新值”。
void ChartWidget::drawLegend(QPainter &painter, const QRectF &plot)
{
    // 图例使用略小的字体，避免抢走曲线区域的注意力。
    QFont legendFont = font();
    legendFont.setPointSizeF(std::max(7.5, font().pointSizeF() - 1.0));
    painter.setFont(legendFont);
    // QFontMetricsF 用于精确计算文本宽度，保证布局不重叠。
    const QFontMetricsF metrics(legendFont);

    // 先把所有通道整理成“标签 + 颜色”的条目，并累加总宽度。
    struct LegendItem
    {
        QString label;
        QColor color;
    };
    std::vector<LegendItem> items;
    qreal totalWidth = 0.0;

    for (const auto &seriesPtr : m_series) {
        const Series &series = *seriesPtr;
        // 默认显示 "--"，有数据时显示最后一个样本值。
        QString valueText = QStringLiteral("--");
        if (series.points.size() > 0)
            valueText = QString::number(series.points.at(series.points.size() - 1).value, 'f', 1);

        LegendItem item;
        item.color = series.color;
        item.label = QStringLiteral("%1  %2").arg(series.name, valueText);
        // 宽度预算：短线 18 + 文本宽 + 右边距 14。
        totalWidth += 18.0 + metrics.horizontalAdvance(item.label) + 14.0;
        items.push_back(item);
    }

    if (items.empty())
        return;

    // 如果从右往左排会超出绘图区左边，就整块省略图例，保证曲线区域完整。
    const qreal startX = plot.right() - totalWidth;
    if (startX < plot.left())
        return;

    // 与标题同一水平带，视觉上并排。
    const qreal centerY = 24.0;
    qreal x = startX;
    for (const LegendItem &item : items) {
        // 颜色短线（相当于图例色块）。
        painter.setPen(QPen(item.color, 3.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(x, centerY), QPointF(x + 14.0, centerY));
        x += 18.0;

        // 文本标签：稍加透明度，避免太抢眼。
        QColor textColor = palette().color(QPalette::Text);
        textColor.setAlpha(210);
        painter.setPen(textColor);
        painter.drawText(QRectF(x, centerY - 9.0,
                                metrics.horizontalAdvance(item.label) + 4.0, 18.0),
                         Qt::AlignLeft | Qt::AlignVCenter, item.label);
        x += metrics.horizontalAdvance(item.label) + 14.0;
    }
}

// 暂停时在绘图区右上角画一个橙色“已暂停”角标。
void ChartWidget::drawPausedBadge(QPainter &painter, const QRectF &plot)
{
    if (!m_paused)
        return;

    // 角标字体加粗。
    QFont badgeFont = font();
    badgeFont.setBold(true);
    painter.setFont(badgeFont);
    const QFontMetricsF metrics(badgeFont);
    const QString text = QStringLiteral("已暂停");
    // 角标宽度 = 文本宽 + 左右内边距。
    const qreal width = metrics.horizontalAdvance(text) + 18.0;
    // 贴在绘图区右上角，向内缩 8px。
    const QRectF badge(plot.right() - width - 8.0, plot.top() + 8.0, width, 22.0);

    // 圆角矩形背景：橙色，带 220 的 alpha（略透出背景）。
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0xF5, 0x9E, 0x0B, 220));
    painter.drawRoundedRect(badge, 4.0, 4.0);

    // 白色文字居中。
    painter.setPen(Qt::white);
    painter.drawText(badge, Qt::AlignCenter, text);
}