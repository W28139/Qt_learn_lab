#include "mainwindow.h"

#include "telemetrymodel.h"
#include "telemetryworker.h"

#include <QCloseEvent>
#include <QDebug>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QStatusBar>
#include <QTableView>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace {

constexpr int kDrainIntervalMs = 40; // GUI 每 40 ms 处理一次队列（25 Hz）
constexpr int kModelCapacity = 2000;

} // namespace

MainWindow::MainWindow(QString host, quint16 port, bool embedded, QWidget *parent)
    : QMainWindow(parent)
    , m_host(std::move(host))
    , m_port(port)
    , m_embedded(embedded)
    , m_model(new TelemetryModel(kModelCapacity, this))
{
    // 构造函数运行在 GUI 线程：记录 GUI 线程 id，供界面和 worker 的 id 对照。
    m_guiThreadId = reinterpret_cast<quintptr>(QThread::currentThreadId());

    buildUi();

    // GUI 侧唯一的周期工作：把队列里的批次批量写进模型。
    // 这个定时器不属于 worker，也不关心网络；即使网络断了，界面仍然在刷新。
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
    setWindowTitle(QStringLiteral("Lab 07 - QThread 工作线程"));

    m_view = new QTableView(this);
    m_view->setModel(m_model);
    m_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setAlternatingRowColors(true);
    m_view->setWordWrap(false);
    m_view->setTextElideMode(Qt::ElideRight);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->verticalHeader()->setVisible(false);
    m_view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_view->verticalHeader()->setDefaultSectionSize(m_view->fontMetrics().height() + 8);

    auto *header = m_view->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    m_view->setColumnWidth(TelemetryModel::SeqColumn, 110);
    m_view->setColumnWidth(TelemetryModel::TimeColumn, 150);
    m_view->setColumnWidth(TelemetryModel::ChannelColumn, 80);
    m_view->setColumnWidth(TelemetryModel::QualityColumn, 80);
    header->setSectionResizeMode(TelemetryModel::ValueColumn, QHeaderView::Stretch);

    m_startButton = new QPushButton(QStringLiteral("开始"), this);
    m_stopButton = new QPushButton(QStringLiteral("停止"), this);
    m_clearButton = new QPushButton(QStringLiteral("清空表格"), this);
    m_stopButton->setEnabled(false);

    m_stateLabel = new QLabel(this);
    m_statsLabel = new QLabel(this);
    m_queueLabel = new QLabel(this);
    m_threadLabel = new QLabel(this);
    for (QLabel *label : {m_stateLabel, m_statsLabel, m_queueLabel})
        label->setMinimumWidth(180);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(8);
    controls->addWidget(m_startButton);
    controls->addWidget(m_stopButton);
    controls->addWidget(m_clearButton);
    controls->addSpacing(12);
    controls->addWidget(m_stateLabel);
    controls->addStretch();
    controls->addWidget(m_statsLabel);
    controls->addWidget(m_queueLabel);

    auto *layout = new QVBoxLayout;
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    layout->addLayout(controls);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_threadLabel);

    auto *central = new QWidget(this);
    central->setLayout(layout);
    setCentralWidget(central);

    statusBar()->showMessage(m_embedded
                                 ? QStringLiteral("数据源：内嵌 mock server 127.0.0.1:%1")
                                       .arg(m_port)
                                 : QStringLiteral("数据源：外部服务端 %1:%2")
                                       .arg(m_host)
                                       .arg(m_port));

    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::startWorker);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::requestStop);
    connect(m_clearButton, &QPushButton::clicked, this, &MainWindow::clearAll);
}

void MainWindow::startWorker()
{
    if (m_thread)
        return;

    // 先清空队列：避免上一次运行残留的批次混进本轮表格。
    m_queue.clear();

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("telemetry-worker"));

    // worker 没有 parent：有 parent 的对象不能被 moveToThread。
    m_worker = new TelemetryWorker(m_host, m_port, &m_queue);
    m_worker->moveToThread(m_thread);

    // started 在新线程里发出，worker 的亲和性也是新线程，因此 start() 会在工作线程执行。
    connect(m_thread, &QThread::started, m_worker, &TelemetryWorker::start);

    // worker 的信号来自工作线程，窗口的槽在 GUI 线程：
    // AutoConnection 会自动变成队列连接，槽函数在 GUI 线程执行，可以直接碰界面。
    connect(m_worker, &TelemetryWorker::stateChanged, this, &MainWindow::onWorkerState);
    connect(m_worker, &TelemetryWorker::statsChanged, this, &MainWindow::onWorkerStats);
    connect(m_worker, &TelemetryWorker::logMessage, this, &MainWindow::onWorkerLog);
    connect(m_worker, &TelemetryWorker::workerThreadId, this,
            &MainWindow::onWorkerThreadId);

    // 官方推荐的生命周期写法：线程结束时在它自己的线程里 deleteLater。
    // 注意 order：先挂 worker 的 deleteLater，再挂窗口的 onThreadFinished；
    // 等 onThreadFinished 执行时，worker 已不可再访问。
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

    // stop() 在工作线程执行，清理完 socket/定时器后由它自己 quit()。
    // 不要在这之后立刻调用 m_thread->quit()：quit 可能抢在队列里的 stop 事件之前，
    // 让 socket/定时器没来得及清理。
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
    else if (m_thread)
        m_thread->quit();
}

void MainWindow::drainQueue()
{
    // takeAll() 是唯一跨线程读点：一次原子取走当前所有批次，避免每批都加锁。
    const QList<TelemetryBatch> batches = m_queue.takeAll();
    if (batches.isEmpty()) {
        updateLabels();
        return;
    }

    // 把多批展开成一个样本列表，只调用一次 appendSamples()，
    // 这样模型只发一次 dataChanged/reset，而不是每批刷新一次。
    QList<TelemetrySample> samples;
    for (const TelemetryBatch &batch : batches)
        samples.append(batch.samples);

    if (!samples.isEmpty()) {
        // 模型和视图只在 GUI 线程更新：worker 线程只往队列里放数据。
        m_model->appendSamples(samples);
        m_consumedSamples += static_cast<quint64>(samples.size());
        m_view->scrollToBottom();
    }
    updateLabels();
}

void MainWindow::clearAll()
{
    // 只清 GUI 侧显示和队列；worker 仍在运行，之后的新数据会继续进来。
    m_queue.clear();
    m_model->clear();
    m_consumedSamples = 0;
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

void MainWindow::onWorkerThreadId(quintptr threadId)
{
    m_workerThreadId = threadId;
    updateLabels();
}

void MainWindow::onThreadFinished()
{
    if (!m_thread)
        return;

    // worker 已由 finished -> deleteLater 删除，这里只清理线程对象。
    // 从这行开始 m_worker 失效，任何地方都不能再访问它。
    m_worker = nullptr;
    m_thread->deleteLater();
    m_thread = nullptr;

    m_startButton->setEnabled(true);
    m_stopButton->setEnabled(false);
    m_stateLabel->setText(QStringLiteral("已停止"));
    qInfo().noquote() << "[gui] worker thread finished";
    updateLabels();
}

void MainWindow::updateLabels()
{
    m_stateLabel->setToolTip(m_stateLabel->text());

    m_statsLabel->setText(QStringLiteral("帧 %1 | 样本 %2 | 重连 %3")
                              .arg(m_frames)
                              .arg(m_samples)
                              .arg(m_reconnects));

    m_queueLabel->setText(QStringLiteral("队列 %1/%2 | 峰值 %3 | 丢弃批次 %4")
                              .arg(m_queue.size())
                              .arg(m_queue.capacity())
                              .arg(m_queue.highWatermark())
                              .arg(m_queue.droppedCount()));

    m_threadLabel->setText(
        QStringLiteral("GUI 线程 %1 | worker 线程 %2 | GUI 已消费样本 %3")
            .arg(QStringLiteral("0x%1").arg(QString::number(m_guiThreadId, 16)),
                 m_workerThreadId == 0
                     ? QStringLiteral("(未启动)")
                     : QStringLiteral("0x%1").arg(QString::number(m_workerThreadId, 16)),
                 QString::number(m_consumedSamples)));
}

void MainWindow::shutdownWorker()
{
    if (!m_thread)
        return;

    m_drainTimer->stop();

    // 和按钮停止同样的顺序：先排队调用 stop()，让 worker 清理网络对象并自己 quit()，
    // 再 wait。顺序不能反：先 quit() 可能让事件循环在 stop 事件执行前就退出。
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
    else
        m_thread->quit();

    // 只有关窗路径才允许在 GUI 线程短暂阻塞等待；正常运行时不要在 GUI 线程 wait。
    if (!m_thread->wait(3000)) {
        qWarning().noquote()
            << "[gui] worker did not stop within 3 s; requesting quit as fallback";
        m_thread->quit();
        if (!m_thread->wait(1000)) {
            // 最后手段：terminate() 可能让 worker 来不及清理，但总比退不出进程好。
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
                             "queue_dropped=%2 queue_peak=%3")
                             .arg(m_consumedSamples)
                             .arg(m_queue.droppedCount())
                             .arg(m_queue.highWatermark());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    shutdownWorker();
    QMainWindow::closeEvent(event);
}