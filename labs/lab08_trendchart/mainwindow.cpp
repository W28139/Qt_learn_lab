#include "mainwindow.h"

#include "chartwidget.h"
#include "telemetryworker.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace {

constexpr int kDrainIntervalMs = 40; // GUI 每 40 ms 取一次队列（25 Hz）

} // namespace

MainWindow::MainWindow(QString host, quint16 port, bool embedded, QWidget *parent)
    : QMainWindow(parent)
    , m_host(std::move(host))
    , m_port(port)
    , m_embedded(embedded)
{
    buildUi();

    // 数据在 worker 线程里收；GUI 只按自己的节奏取队列、追加给图表、重绘。
    m_drainTimer = new QTimer(this);
    m_drainTimer->setInterval(kDrainIntervalMs);
    connect(m_drainTimer, &QTimer::timeout, this, &MainWindow::drainQueue);
    m_drainTimer->start();

    updateLabels();
    startWorker();
}

MainWindow::~MainWindow()
{
    shutdownWorker();
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Lab 08 - QPainter 实时趋势图"));
    resize(1100, 680);

    m_chart = new ChartWidget(this);

    m_startButton = new QPushButton(QStringLiteral("开始"), this);
    m_stopButton = new QPushButton(QStringLiteral("停止"), this);
    m_clearButton = new QPushButton(QStringLiteral("清空数据"), this);
    m_stopButton->setEnabled(false);

    m_pauseButton = new QPushButton(QStringLiteral("暂停"), this);
    m_pauseButton->setCheckable(true);
    m_pauseButton->setToolTip(QStringLiteral("冻结时间轴；后台仍继续接收数据"));

    m_windowCombo = new QComboBox(this);
    m_windowCombo->addItem(QStringLiteral("5 秒"), 5.0);
    m_windowCombo->addItem(QStringLiteral("10 秒"), 10.0);
    m_windowCombo->addItem(QStringLiteral("30 秒"), 30.0);
    m_windowCombo->addItem(QStringLiteral("60 秒"), 60.0);
    m_windowCombo->setCurrentIndex(1);
    m_windowCombo->setToolTip(QStringLiteral("横轴可见的时间窗口"));

    m_autoScaleBox = new QCheckBox(QStringLiteral("自动缩放"), this);
    m_autoScaleBox->setChecked(true);
    m_autoScaleBox->setToolTip(QStringLiteral("按窗口内数据自动计算 Y 轴范围"));

    m_gridBox = new QCheckBox(QStringLiteral("网格"), this);
    m_gridBox->setChecked(true);

    m_stateLabel = new QLabel(this);
    m_statsLabel = new QLabel(this);
    m_queueLabel = new QLabel(this);
    m_chartLabel = new QLabel(this);
    m_stateLabel->setMinimumWidth(220);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(8);
    controls->addWidget(m_startButton);
    controls->addWidget(m_stopButton);
    controls->addWidget(m_clearButton);
    controls->addSpacing(12);
    controls->addWidget(m_pauseButton);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(QStringLiteral("窗口："), this));
    controls->addWidget(m_windowCombo);
    controls->addWidget(m_autoScaleBox);
    controls->addWidget(m_gridBox);
    controls->addStretch();
    controls->addWidget(m_stateLabel);

    auto *info = new QHBoxLayout;
    info->setSpacing(24);
    info->addWidget(m_statsLabel);
    info->addWidget(m_queueLabel);
    info->addWidget(m_chartLabel);
    info->addStretch();

    auto *layout = new QVBoxLayout;
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    layout->addLayout(controls);
    layout->addWidget(m_chart, 1);
    layout->addLayout(info);

    auto *central = new QWidget(this);
    central->setLayout(layout);
    setCentralWidget(central);

    statusBar()->showMessage(
        m_embedded
            ? QStringLiteral("数据源：内嵌 mock server 127.0.0.1:%1").arg(m_port)
            : QStringLiteral("数据源：外部服务端 %1:%2").arg(m_host).arg(m_port));

    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::startWorker);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::requestStop);
    connect(m_clearButton, &QPushButton::clicked, this, &MainWindow::clearAll);
    connect(m_pauseButton, &QPushButton::toggled, this, &MainWindow::togglePause);
    connect(m_windowCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        m_chart->setWindowSeconds(m_windowCombo->currentData().toDouble());
    });
    connect(m_autoScaleBox, &QCheckBox::toggled, m_chart, &ChartWidget::setAutoScale);
    connect(m_gridBox, &QCheckBox::toggled, m_chart, &ChartWidget::setShowGrid);

    // 让图表初始窗口与下拉框当前项保持一致（不依赖 ChartWidget 的默认值）。
    m_chart->setWindowSeconds(m_windowCombo->currentData().toDouble());
}

void MainWindow::startWorker()
{
    if (m_thread)
        return;

    m_queue.clear();

    // 从暂停状态启动时先恢复，否则新数据会写到冻结窗口的右边看不见。
    if (m_pauseButton->isChecked())
        m_pauseButton->setChecked(false);

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("telemetry-worker"));

    m_worker = new TelemetryWorker(m_host, m_port, &m_queue);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker, &TelemetryWorker::start);
    connect(m_worker, &TelemetryWorker::stateChanged, this, &MainWindow::onWorkerState);
    connect(m_worker, &TelemetryWorker::statsChanged, this, &MainWindow::onWorkerStats);
    connect(m_worker, &TelemetryWorker::logMessage, this, &MainWindow::onWorkerLog);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, this, &MainWindow::onThreadFinished);

    m_startButton->setEnabled(false);
    m_stopButton->setEnabled(true);
    m_stateLabel->setText(QStringLiteral("启动中…"));
    m_thread->start();
}

void MainWindow::requestStop()
{
    if (!m_thread)
        return;

    m_stopButton->setEnabled(false);
    m_stateLabel->setText(QStringLiteral("停止中…"));

    // stop() 在 worker 线程清理 socket/timer 后自己 quit；不要在 GUI 线程抢先 quit。
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
    else
        m_thread->quit();
}

void MainWindow::drainQueue()
{
    const QList<TelemetryBatch> batches = m_queue.takeAll();
    if (batches.isEmpty()) {
        updateLabels();
        return;
    }

    QList<TelemetrySample> samples;
    for (const TelemetryBatch &batch : batches)
        samples.append(batch.samples);

    if (!samples.isEmpty()) {
        // 一次追加整批样本，ChartWidget 内部只触发一次 update()。
        m_chart->appendSamples(samples);
        m_consumedSamples += static_cast<quint64>(samples.size());
    }

    updateLabels();
}

void MainWindow::clearAll()
{
    m_queue.clear();
    m_chart->clearData();
    m_consumedSamples = 0;
    updateLabels();
}

void MainWindow::togglePause(bool paused)
{
    m_chart->setPaused(paused);
    m_pauseButton->setText(paused ? QStringLiteral("继续") : QStringLiteral("暂停"));
    updateLabels();
}

void MainWindow::onWorkerState(int state, const QString &detail)
{
    m_stateLabel->setText(QStringLiteral("%1：%2")
                              .arg(TelemetryWorker::stateName(
                                       static_cast<TelemetryWorker::State>(state)),
                                   detail));
    statusBar()->showMessage(detail, 5000);
    if (state == static_cast<int>(TelemetryWorker::State::Failed))
        qWarning().noquote() << "[gui] worker failed:" << detail;
    updateLabels();
}

void MainWindow::onWorkerStats(quint64 frames, quint64 samples, quint64 reconnects)
{
    m_frames = frames;
    m_samples = samples;
    m_reconnects = reconnects;
    updateLabels();
}

void MainWindow::onWorkerLog(const QString &text)
{
    qInfo().noquote() << "[worker]" << text;
}

void MainWindow::onThreadFinished()
{
    if (!m_thread)
        return;

    // worker 已由 finished -> deleteLater 删除，从这里开始不能再访问 m_worker。
    m_worker = nullptr;
    m_thread->deleteLater();
    m_thread = nullptr;

    m_startButton->setEnabled(true);
    m_stopButton->setEnabled(false);
    m_stateLabel->setText(QStringLiteral("已停止"));
    qInfo().noquote() << "[gui] worker thread finished";
    updateLabels();
}

// 状态栏刷新函数：把内部各处的计数器/队列状态/图表性能指标，一次性写进 4 个 QLabel
void MainWindow::updateLabels()
{
    m_stateLabel->setToolTip(m_stateLabel->text());

    m_statsLabel->setText(QStringLiteral("帧 %1 | 样本 %2 | 重连 %3")
                              .arg(m_frames)
                              .arg(m_samples)
                              .arg(m_reconnects));

    m_queueLabel->setText(QStringLiteral("队列 %1/%2 | 峰值 %3 | 丢弃 %4")
                              .arg(m_queue.size())
                              .arg(m_queue.capacity())
                              .arg(m_queue.highWatermark())
                              .arg(m_queue.droppedCount()));

    // 这三个数字是本单元最直观的性能反馈：重绘频率、单次耗时、窗口内点数。
    m_chartLabel->setText(QStringLiteral("绘制 %1 次 | 最后 %2 ms | 点 %3 | 已消费 %4")
                              .arg(m_chart->paintCount())
                              .arg(m_chart->lastPaintMs(), 0, 'f', 2)
                              .arg(m_chart->paintedPointCount())
                              .arg(m_consumedSamples));
}

void MainWindow::shutdownWorker()
{
    if (!m_thread)
        return;

    m_drainTimer->stop();

    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
    else
        m_thread->quit();

    if (!m_thread->wait(3000)) {
        qWarning().noquote()
            << "[gui] worker did not stop within 3 s; requesting quit as fallback";
        m_thread->quit();
        if (!m_thread->wait(1000)) {
            qWarning().noquote()
                << "[gui] worker thread still alive; terminating as last resort";
            m_thread->terminate();
            m_thread->wait(1000);
        }
    }

    m_worker = nullptr; // 正常路径已由 finished -> deleteLater 删除
    delete m_thread;
    m_thread = nullptr;

    qInfo().noquote() << QStringLiteral(
                             "[gui] shutdown complete: gui_consumed=%1 "
                             "queue_dropped=%2 queue_peak=%3 paints=%4")
                             .arg(m_consumedSamples)
                             .arg(m_queue.droppedCount())
                             .arg(m_queue.highWatermark())
                             .arg(m_chart->paintCount());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    shutdownWorker();
    QMainWindow::closeEvent(event);
}