#include "telemetrymodel.h"

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QRandomGenerator>
#include <QStatusBar>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>

namespace {

constexpr int kChannelCount = 4;
constexpr int kSampleRateHz = 100;          // 每个通道每秒 100 个样本
constexpr int kBatchIntervalMs = 100;       // 界面每 100 ms 刷新一次
constexpr int kRingCapacity = 5000;         // 4 通道 × 100 Hz ≈ 12.5 秒历史

} // namespace

// 遥测数据演示页，内部用 QTimer 周期生成模拟采样
class TelemetryPage final : public QWidget
{
    Q_OBJECT

public:
    // 构造：初始化模型、搭建 UI、创建生成与刷新两个定时器，并刷新首帧统计。
    explicit TelemetryPage(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_model(new TelemetryModel(kRingCapacity, this))  // 固定容量环形缓冲模型
    {
        // 搭建控件、布局、信号连接
        buildUi();
        // 生成定时器：按目标采样率触发，使用 PreciseTimer 尽量减少抖动。
        // 每次 tick 会为每个通道各生成一条样本
        m_generateTimer = new QTimer(this);
        m_generateTimer->setTimerType(Qt::PreciseTimer);
        m_generateTimer->setInterval(1000 / kSampleRateHz);
        connect(m_generateTimer, &QTimer::timeout, this, &TelemetryPage::generateTick);

        // 刷新定时器：把累积在 m_pending 的样本批量写入模型
        // 避免逐条 append 触发大量 insert 信号拖慢视图。
        m_flushTimer = new QTimer(this);
        m_flushTimer->setInterval(kBatchIntervalMs);
        connect(m_flushTimer, &QTimer::timeout, this, &TelemetryPage::flushBatch);

        m_pending.reserve(256); // 预分配，减少批量期间的内存碎片
        updateRunUi();          // 初始化按钮文字为「开始采集」
        updateStats();          // 初始化速率/缓冲标签
    }

signals:
    // 统计信息变化信号：总接收数、当前缓冲数、容量、实时速率（样本/秒）。
    // 供外部（如状态栏）监听并显示。
    void statsChanged(quint64 total, int buffered, int capacity, double rate);

private slots:
    // 生成定时器回调：为每个通道生成一条模拟样本，追加到 m_pending
    void generateTick()
    {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        m_phase += 0.02;

        for (int ch = 0; ch < kChannelCount; ++ch) {
            const double noise =
                (QRandomGenerator::global()->bounded(2001) - 1000) / 1000.0;

            TelemetrySample sample;
            sample.timestampMs = nowMs;
            sample.seq = ++m_seq;
            sample.channel = quint16(ch);
            sample.value = 50.0 + 12.0 * std::sin(m_phase + ch * 0.8) + noise;

            // 故意制造少量可复现的告警/异常，方便观察颜色
            if (sample.seq % 997 == 0)
                sample.quality = Quality::Bad;
            else if (sample.seq % 113 == 0)
                sample.quality = Quality::Warning;

            m_pending.append(sample);
            ++m_generated;
        }
    }

    // 刷新定时器回调：把 m_pending 批量写入模型，可选自动滚到底部，并更新统计。
    void flushBatch()
    {
        if (!m_pending.isEmpty()) {
            m_model->appendSamples(m_pending);
            m_pending.clear();

            // 用户勾选「自动跟随最新」时，把视图滚到最底部。
            if (m_followCheck->isChecked())
                m_view->scrollToBottom();
        }
        updateStats();
    }

    // 开始/停止切换
    void toggleRunning()
    {
        if (m_running)
            stopRunning();
        else
            startRunning();
    }

    // 清空：重置模型、暂存、计数器、速率统计、相位，但保留「是否运行」状态。
    void clearAll()
    {
        m_model->clear();
        m_pending.clear();
        m_generated = 0;
        m_seq = 0;
        m_rateBaseCount = 0;
        m_rateBaseMs = 0;
        m_rate = 0.0;
        m_phase = 0.0;
        if (m_running)
            m_runClock.restart();
        updateStats();
    }

private:
    void buildUi()
    {
        // 创建表格视图并挂载模型
        m_view = new QTableView(this);
        m_view->setModel(m_model);

        // 等宽字体便于对齐数字/时间列。
        const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        m_view->setFont(mono);

        // 视图为只读：不允许编辑、按行选中、单选、隔行变色、单行显示不换行、超长省略。
        m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_view->setSelectionMode(QAbstractItemView::SingleSelection);
        m_view->setAlternatingRowColors(true);
        m_view->setWordWrap(false);
        m_view->setTextElideMode(Qt::ElideRight);
        m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

        // 隐藏垂直表头（行号），固定行高 = 字体高度 + 8，避免不同 DPI 下拥挤。
        m_view->verticalHeader()->setVisible(false);
        m_view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        m_view->verticalHeader()->setDefaultSectionSize(m_view->fontMetrics().height() + 8);

        // 列宽：前三列和状态列给固定/交互宽度，Value 列自动拉伸填满剩余空间
        auto *header = m_view->horizontalHeader();
        header->setSectionResizeMode(QHeaderView::Interactive);
        header->setStretchLastSection(false);
        m_view->setColumnWidth(TelemetryModel::SeqColumn, 110);
        m_view->setColumnWidth(TelemetryModel::TimeColumn, 160);
        m_view->setColumnWidth(TelemetryModel::ChannelColumn, 90);
        m_view->setColumnWidth(TelemetryModel::QualityColumn, 90);
        header->setSectionResizeMode(TelemetryModel::ValueColumn,
                                     QHeaderView::Stretch);

        // 工具栏控件：开始/停止、清空、自动跟随复选框、两个统计标签。
        m_toggleButton = new QPushButton(this);
        m_clearButton = new QPushButton(QStringLiteral("清空"), this);
        m_followCheck = new QCheckBox(QStringLiteral("自动跟随最新"), this);
        m_followCheck->setChecked(true);

        m_rateLabel = new QLabel(this);
        m_bufferLabel = new QLabel(this);
        m_rateLabel->setMinimumWidth(150);
        m_bufferLabel->setMinimumWidth(190);

        // 控制栏水平布局：左侧按钮组 + 弹簧 + 右侧统计标签。
        auto *controls = new QHBoxLayout;
        controls->setSpacing(8);
        controls->addWidget(m_toggleButton);
        controls->addWidget(m_clearButton);
        controls->addSpacing(12);
        controls->addWidget(m_followCheck);
        controls->addStretch();
        controls->addWidget(m_rateLabel);
        controls->addWidget(m_bufferLabel);

        // 页面主布局：上控制栏，下表格（拉伸系数 1）。
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(8);
        layout->addLayout(controls);
        layout->addWidget(m_view, 1);

        // 按钮信号连接
        connect(m_toggleButton, &QPushButton::clicked,
                this, &TelemetryPage::toggleRunning);
        connect(m_clearButton, &QPushButton::clicked,
                this, &TelemetryPage::clearAll);
    }

    // 开始采集：启动两个定时器，重置速率统计基线，更新按钮文字。
    void startRunning()
    {
        if (m_running)
            return;

        m_running = true;
        m_runClock.start();     // 从头计时，作为速率统计的时间基准
        m_rateBaseMs = 0;
        m_rateBaseCount = m_generated;  // 以当前累计生成数为速率基线
        m_rate = 0.0;
        m_generateTimer->start();
        m_flushTimer->start();
        updateRunUi();
        updateStats();
    }

    // 停止采集：停两个定时器；把 m_pending 中尚未刷入模型的样本补刷一次，避免丢尾；
    // 速率清零并刷新 UI 与统计。
    void stopRunning()
    {
        if (!m_running)
            return;

        m_running = false;
        m_generateTimer->stop();
        m_flushTimer->stop();

        // 停止后立即 flush 一次，保证用户能看到最后一批数据。
        if (!m_pending.isEmpty()) {
            m_model->appendSamples(m_pending);
            m_pending.clear();
            if (m_followCheck->isChecked())
                m_view->scrollToBottom();
        }

        m_rate = 0.0;
        updateRunUi();
        updateStats();
    }

    // 同步「运行/停止」状态到按钮文字。
    void updateRunUi()
    {
        m_toggleButton->setText(m_running ? QStringLiteral("停止采集")
                                          : QStringLiteral("开始采集"));
    }

    // 更新统计标签并向外部发 statsChanged 信号，更新UI页面
    void updateStats()
    {
        // 估算数据生成速率 m_rate,用于 UI 展示
        if (m_running) {
            const qint64 nowMs = m_runClock.elapsed();
            const qint64 spanMs = nowMs - m_rateBaseMs;
            if (spanMs >= 500) {
                m_rate = double(m_generated - m_rateBaseCount) * 1000.0
                         / double(spanMs);
                m_rateBaseMs = nowMs;
                m_rateBaseCount = m_generated;
            }
        }
        else {
            m_rate = 0.0;
        }

        const int buffered = m_model->bufferedCount();
        const int capacity = m_model->capacity();

        // 更新UI页面的两个标签
        m_rateLabel->setText(QStringLiteral("速率：%1 样本/秒")
                                 .arg(m_rate, 0, 'f', 0));
        m_bufferLabel->setText(QStringLiteral("缓冲：%1 / %2")
                                   .arg(buffered)
                                   .arg(capacity));

        emit statsChanged(m_model->totalReceived(), buffered, capacity, m_rate);
    }

    TelemetryModel *m_model = nullptr;         // 数据模型（固定容量环形缓冲）
    QTableView *m_view = nullptr;              // 数据表格视图
    QPushButton *m_toggleButton = nullptr;     // 开始/停止按钮（文字随状态切换）
    QPushButton *m_clearButton = nullptr;      // 清空按钮
    QCheckBox *m_followCheck = nullptr;        // 「自动跟随最新」复选框
    QLabel *m_rateLabel = nullptr;             // 速率统计标签
    QLabel *m_bufferLabel = nullptr;           // 缓冲占用统计标签
    QTimer *m_generateTimer = nullptr;         // 生成定时器（高频、精确）
    QTimer *m_flushTimer = nullptr;            // 刷新定时器（低频、批量入模型）

    QList<TelemetrySample> m_pending;          // 待刷入模型的样本暂存列表
    QElapsedTimer m_runClock;                  // 运行时长计时器，用于速率统计
    quint64 m_generated = 0;                   // 累计生成样本数（含已淘汰的）
    quint64 m_seq = 0;                         // 全局递增序号
    quint64 m_rateBaseCount = 0;               // 速率统计窗口起点的样本计数
    qint64 m_rateBaseMs = 0;                   // 速率统计窗口起点的时间（相对 m_runClock）
    double m_rate = 0.0;                       // 最近一次计算的样本速率（样本/秒）
    double m_phase = 0.0;                      // 正弦波相位，用于生成平滑变化的模拟值
    bool m_running = false;                    // 是否处于采集运行状态

};

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow()
    {
        setWindowTitle(QStringLiteral("Lab 04 - Telemetry Model/View"));
        resize(1000, 640);

        auto *page = new TelemetryPage(this);
        setCentralWidget(page);

        auto *totalLabel = new QLabel(QStringLiteral("总样本：0"), this);
        statusBar()->addPermanentWidget(totalLabel);
        statusBar()->showMessage(
            QStringLiteral("100 Hz × 4 通道 · UI 每 %1 ms 批量刷新")
                .arg(kBatchIntervalMs));

        connect(page, &TelemetryPage::statsChanged, this,
                [totalLabel](quint64 total, int, int, double) {
                    totalLabel->setText(QStringLiteral("总样本：%1").arg(total));
                });
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}

// 带 Q_OBJECT 的类定义在 main.cpp 内时，需要显式包含 moc 生成代码
#include "main.moc"