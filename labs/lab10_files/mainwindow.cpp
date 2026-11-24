#include "mainwindow.h"

#include "appsettings.h"
#include "csvexportworker.h"
#include "dbwriter.h"
#include "historypanel.h"
#include "livemodel.h"
#include "logging.h"
#include "settingsdialog.h"
#include "telemetryworker.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressDialog>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <climits>
#include <utility>

namespace {

constexpr int kDrainIntervalMs = 40; // GUI 每 40 ms 取一次队列

} // namespace

MainWindow::MainWindow(const apppaths::Paths &paths, QString host, quint16 port,
                       bool embedded, AppSettings &settings, QWidget *parent)
    : QMainWindow(parent)
    , m_paths(paths)
    , m_settings(&settings)
    , m_dbPath(paths.dbPath)
    , m_host(std::move(host))
    , m_port(port)
    , m_embedded(embedded)
{
    buildUi();

    const QByteArray geometry = m_settings->windowGeometry();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    const QByteArray state = m_settings->windowState();
    if (!state.isEmpty())
        restoreState(state);

    m_history->setPageSize(m_settings->pageSize(100));

    m_drainTimer = new QTimer(this);
    m_drainTimer->setInterval(kDrainIntervalMs);
    connect(m_drainTimer, &QTimer::timeout, this, &MainWindow::drainQueue);

    // ---- DB writer 线程 ----
    m_dbThread = new QThread(this);
    m_dbThread->setObjectName(QStringLiteral("db-writer"));

    m_writer = new DbWriter(m_dbPath);
    m_writer->moveToThread(m_dbThread);

    connect(m_dbThread, &QThread::started, m_writer, &DbWriter::open);
    connect(m_writer, &DbWriter::opened, this, &MainWindow::onDbOpened);
    connect(m_writer, &DbWriter::sessionStarted, this, &MainWindow::onDbSessionStarted);
    connect(m_writer, &DbWriter::sessionFinished, this, &MainWindow::onDbSessionFinished);
    connect(m_writer, &DbWriter::statsChanged, this, &MainWindow::onDbStats);
    connect(m_writer, &DbWriter::errorOccurred, this, &MainWindow::onDbError);
    connect(m_writer, &DbWriter::cleared, this, &MainWindow::onDbCleared);
    connect(m_writer, &DbWriter::logMessage, this, [](const QString &text) {
        qInfo().noquote() << "[db]" << text;
    });
    connect(m_dbThread, &QThread::finished, m_writer, &QObject::deleteLater);

    // GUI -> writer 的全部调用都走队列连接：参数被复制，两个线程不共享对象。
    connect(this, &MainWindow::beginSessionRequested, m_writer, &DbWriter::beginSession);
    connect(this, &MainWindow::persistSamples, m_writer, &DbWriter::appendSamples);
    connect(this, &MainWindow::endSessionRequested, m_writer, &DbWriter::endSession);

    m_dbThread->start();

    updateLabels();
}

MainWindow::~MainWindow()
{
    shutdownAll();
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Lab 10 - 文件、配置、CSV 与日志"));
    resize(1180, 760);

    m_startButton = new QPushButton(QStringLiteral("开始"), this);
    m_stopButton = new QPushButton(QStringLiteral("停止"), this);
    m_clearButton = new QPushButton(QStringLiteral("清空历史"), this);
    m_clearButton->setToolTip(QStringLiteral("停止采集后，删除所有会话和样本"));
    m_startButton->setEnabled(false); // 等数据库 ready
    m_stopButton->setEnabled(false);
    m_clearButton->setEnabled(false);

    m_settingsButton = new QPushButton(QStringLiteral("设置…"), this);
    m_openLogButton = new QPushButton(QStringLiteral("日志目录"), this);
    m_openLogButton->setToolTip(m_paths.logDir);

    m_stateLabel = new QLabel(QStringLiteral("等待数据库…"), this);
    m_stateLabel->setMinimumWidth(240);
    m_workerLabel = new QLabel(this);
    m_queueLabel = new QLabel(this);
    m_dbLabel = new QLabel(this);
    m_logLabel = new QLabel(this);
    m_logLabel->setToolTip(m_paths.logFile);
    m_logLabel->setText(QStringLiteral("日志：%1").arg(QFileInfo(m_paths.logFile).fileName()));

    auto *controls = new QHBoxLayout;
    controls->setSpacing(8);
    controls->addWidget(m_startButton);
    controls->addWidget(m_stopButton);
    controls->addWidget(m_clearButton);
    controls->addSpacing(16);
    controls->addWidget(m_stateLabel);
    controls->addStretch();
    controls->addWidget(m_settingsButton);
    controls->addWidget(m_openLogButton);

    auto *info = new QHBoxLayout;
    info->setSpacing(24);
    info->addWidget(m_workerLabel);
    info->addWidget(m_queueLabel);
    info->addWidget(m_dbLabel);
    info->addStretch();

    // 实时页：只展示最近 200 条；完整数据在数据库里。
    m_liveModel = new LiveSampleModel(this);
    m_liveTable = new QTableView(this);
    m_liveTable->setModel(m_liveModel);
    m_liveTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_liveTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_liveTable->setAlternatingRowColors(true);
    m_liveTable->verticalHeader()->setVisible(false);
    m_liveTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_liveTable->horizontalHeader()->setStretchLastSection(true);
    m_liveTable->setColumnWidth(LiveSampleModel::SeqColumn, 80);
    m_liveTable->setColumnWidth(LiveSampleModel::TimeColumn, 130);
    m_liveTable->setColumnWidth(LiveSampleModel::ChannelColumn, 80);
    m_liveTable->setColumnWidth(LiveSampleModel::ValueColumn, 100);

    auto *livePage = new QWidget(this);
    auto *liveLayout = new QVBoxLayout(livePage);
    liveLayout->setContentsMargins(8, 8, 8, 8);
    liveLayout->addWidget(new QLabel(
        QStringLiteral("实时样本（仅显示最近 200 条，全部数据同时写入 SQLite）"), livePage));
    liveLayout->addWidget(m_liveTable, 1);

    // 历史页：自己的读连接，在 onDbOpened() 里打开。
    m_history = new HistoryPanel(m_dbPath, this);
    connect(m_history, &HistoryPanel::logMessage, this, [](const QString &text) {
        qInfo().noquote() << "[history]" << text;
    });
    connect(m_history, &HistoryPanel::exportRequested,
            this, &MainWindow::onHistoryExportRequested);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(livePage, QStringLiteral("实时样本"));
    m_tabs->addTab(m_history, QStringLiteral("历史查询"));

    auto *layout = new QVBoxLayout;
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    layout->addLayout(controls);
    layout->addWidget(m_tabs, 1);
    layout->addLayout(info);

    auto *central = new QWidget(this);
    central->setLayout(layout);
    setCentralWidget(central);

    statusBar()->addPermanentWidget(m_logLabel);
    statusBar()->showMessage(
        QStringLiteral("%1 | 数据库：%2 | 数据目录：%3")
            .arg(m_embedded
                     ? QStringLiteral("数据源：内嵌 mock server 127.0.0.1:%1").arg(m_port)
                     : QStringLiteral("数据源：外部服务端 %1:%2").arg(m_host).arg(m_port),
                 m_dbPath,
                 m_paths.dataDir));

    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::startWorker);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::requestStop);
    connect(m_clearButton, &QPushButton::clicked, this, &MainWindow::clearHistory);
    connect(m_settingsButton, &QPushButton::clicked, this, &MainWindow::showSettingsDialog);
    connect(m_openLogButton, &QPushButton::clicked, this, [this] {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(m_paths.logDir)))
            statusBar()->showMessage(
                QStringLiteral("无法打开日志目录：%1").arg(m_paths.logDir), 5000);
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1 && m_history->isOpen())
            m_history->refreshSessions();
    });
}

void MainWindow::onDbOpened(const QString &path, const QString &error)
{
    if (!error.isEmpty()) {
        m_dbOk = false;
        m_stateLabel->setText(QStringLiteral("数据库不可用"));
        m_dbLabel->setText(QStringLiteral("DB：打开失败"));
        m_startButton->setEnabled(false);
        m_clearButton->setEnabled(false);
        qCritical().noquote() << "[gui] database open failed:" << error;
        statusBar()->showMessage(QStringLiteral("数据库打开失败：%1").arg(error));
        return;
    }

    m_dbOk = true;
    m_dbPath = path;

    QString historyError;
    if (!m_history->open(&historyError))
        qWarning().noquote() << "[gui] history reader disabled:" << historyError;

    m_startButton->setEnabled(true);
    m_clearButton->setEnabled(true);
    statusBar()->showMessage(
        QStringLiteral("数据库就绪：%1（WAL，DB writer 线程 + GUI 读连接）").arg(path));
    qInfo().noquote() << "[gui] database ready:" << path;

    // 与前面单元一致：默认启动后自动开始采集；可在“设置”里关闭。
    if (m_settings->autoStart(true)) {
        QTimer::singleShot(0, this, &MainWindow::startWorker);
    } else {
        m_stateLabel->setText(QStringLiteral("就绪（未自动开始）"));
        qInfo().noquote() << "[gui] auto start disabled in settings";
    }
}

void MainWindow::startWorker()
{
    if (m_thread || !m_dbOk || m_shuttingDown)
        return;

    m_queue.clear();
    m_liveModel->clear();
    m_consumedSamples = 0;

    // 会话先开始，worker 后启动；样本要经过 GUI 取队列才会发给 writer，
    // 此时 beginSession 已经排在 writer 事件队列前面。
    m_sessionActive = true;
    emit beginSessionRequested();

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
    m_clearButton->setEnabled(false);
    m_stateLabel->setText(QStringLiteral("启动中…"));

    m_drainTimer->start();
    m_thread->start();
}

void MainWindow::requestStop()
{
    if (!m_thread)
        return;

    m_stopButton->setEnabled(false);
    m_stateLabel->setText(QStringLiteral("停止中…"));

    // stop() 在 worker 线程清理完 socket 后自己 quit；
    // 停止期间 drain timer 继续运行，把 worker 最后推送的批次取出来。
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
    else
        m_thread->quit();
}

void MainWindow::onThreadFinished()
{
    if (!m_thread || m_shuttingDown)
        return;

    // worker 已由 finished -> deleteLater 删除，不能再访问 m_worker。
    m_worker = nullptr;
    m_thread->deleteLater();
    m_thread = nullptr;

    m_drainTimer->stop();

    // 最终排空：此时 worker 不会再 push，取完就是本轮全部样本。
    drainQueue();

    // 最后一批已经 emit 给 writer；endSession 排在它后面，保证不丢数据。
    m_sessionActive = false;
    emit endSessionRequested();

    m_startButton->setEnabled(m_dbOk);
    m_stopButton->setEnabled(false);
    m_clearButton->setEnabled(m_dbOk);
    m_stateLabel->setText(QStringLiteral("已停止"));
    qInfo().noquote() << "[gui] worker thread finished";
    updateLabels();
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
        m_liveModel->appendSamples(samples);
        m_liveTable->scrollToBottom();
        m_consumedSamples += static_cast<quint64>(samples.size());

        // 同一批既进界面模型，也发给 DB 线程；GUI 自己不做任何 SQL。
        if (m_sessionActive)
            emit persistSamples(samples);
    }

    updateLabels();
}

void MainWindow::clearHistory()
{
    if (!m_dbOk || m_sessionActive || m_exportThread)
        return;
    QMetaObject::invokeMethod(m_writer, &DbWriter::clearAll, Qt::QueuedConnection);
}

void MainWindow::onDbSessionStarted(qint64 sessionId, qint64 startedMs)
{
    m_sessionId = sessionId;
    qInfo().noquote() << QStringLiteral("[gui] session #%1 started at %2")
                             .arg(sessionId)
                             .arg(QDateTime::fromMSecsSinceEpoch(startedMs)
                                      .toString(QStringLiteral("HH:mm:ss")));
    if (m_history->isOpen())
        m_history->refreshSessions();
    updateLabels();
}

void MainWindow::onDbSessionFinished(qint64 sessionId, qint64 sampleCount)
{
    m_sessionId = 0;
    qInfo().noquote() << QStringLiteral("[gui] session #%1 finished, %2 rows")
                             .arg(sessionId)
                             .arg(sampleCount);
    if (m_history->isOpen())
        m_history->refreshSessions();
    updateLabels();
}

void MainWindow::onDbStats(quint64 rowsWritten, quint64 commits, qint64 lastCommitUs,
                           qint64 pendingRows)
{
    m_dbRows = rowsWritten;
    m_dbCommits = commits;
    m_dbLastCommitUs = lastCommitUs;
    m_dbPending = pendingRows;
    updateLabels();
}

void MainWindow::onDbError(const QString &text)
{
    qWarning().noquote() << "[db] error:" << text;
    statusBar()->showMessage(QStringLiteral("数据库错误：%1").arg(text), 8000);
}

void MainWindow::onDbCleared()
{
    m_liveModel->clear();
    if (m_history->isOpen())
        m_history->refreshSessions();
    m_history->setExportStatus(QStringLiteral("历史数据已清空"));
    statusBar()->showMessage(QStringLiteral("历史数据已清空"), 5000);
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

void MainWindow::showHistoryTab()
{
    m_tabs->setCurrentIndex(1);
    if (m_history->isOpen())
        m_history->refreshSessions();
}

void MainWindow::stopCapture()
{
    requestStop();
}

void MainWindow::showSettingsDialog()
{
    SettingsDialog dialog(*m_settings, m_dbPath,
                          m_settings->exportDir(m_paths.exportDir), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    // 能立即生效的：页大小、日志级别、导出目录、BOM（导出时再读）。
    m_history->setPageSize(dialog.pageSize());
    applyLogLevel(dialog.logLevel());

    qCInfo(lcApp).noquote() << "settings saved:" << m_settings->fileName();
    statusBar()->showMessage(
        QStringLiteral("设置已保存；数据库路径和“自动开始”在下次启动时生效"), 7000);
}

void MainWindow::applyLogLevel(const QString &level)
{
    bool ok = false;
    const logging::Level parsed = logging::levelFromString(level, &ok);
    if (!ok) {
        qWarning().noquote() << "[settings] invalid log level:" << level;
        return;
    }
    logging::setMinimumLevel(parsed);
    qCInfo(lcApp).noquote() << "log level set to" << logging::levelToString(parsed);
}

void MainWindow::exportSelectedSession()
{
    const qint64 sessionId = m_history->hasSession() ? m_history->currentSessionId()
                                                     : m_sessionId;
    if (sessionId <= 0) {
        statusBar()->showMessage(QStringLiteral("没有可导出的会话"), 5000);
        return;
    }
    onHistoryExportRequested(sessionId);
}

void MainWindow::onHistoryExportRequested(qint64 sessionId)
{
    if (m_exportThread) {
        statusBar()->showMessage(QStringLiteral("已有导出任务在运行"), 4000);
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出会话 #%1 为 CSV").arg(sessionId),
        defaultExportPath(sessionId),
        QStringLiteral("CSV 文件 (*.csv);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    startExport(sessionId, path, true);
}

void MainWindow::exportSelectedSessionAuto()
{
    if (m_exportThread) {
        qWarning().noquote() << "[export] another export is already running";
        return;
    }

    const qint64 sessionId = m_history->hasSession() ? m_history->currentSessionId()
                                                     : m_sessionId;
    if (sessionId <= 0) {
        qWarning().noquote() << "[export] no session available for auto export";
        return;
    }

    startExport(sessionId, defaultExportPath(sessionId), false);
}

QString MainWindow::defaultExportPath(qint64 sessionId) const
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    return QDir(m_settings->exportDir(m_paths.exportDir))
        .filePath(QStringLiteral("session-%1-%2.csv").arg(sessionId).arg(stamp));
}

bool MainWindow::startExport(qint64 sessionId, const QString &path, bool interactive)
{
    if (m_exportThread)
        return false;
    if (sessionId <= 0) {
        statusBar()->showMessage(QStringLiteral("没有可导出的会话"), 5000);
        return false;
    }

    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        QMessageBox::warning(this, QStringLiteral("导出 CSV"),
                             QStringLiteral("无法创建目录：%1")
                                 .arg(QFileInfo(path).absolutePath()));
        return false;
    }

    m_exportInteractive = interactive;

    if (interactive) {
        m_progress = new QProgressDialog(
            QStringLiteral("正在导出会话 #%1…").arg(sessionId), QStringLiteral("取消"),
            0, 0, this);
        m_progress->setWindowTitle(QStringLiteral("导出 CSV"));
        m_progress->setWindowModality(Qt::WindowModal);
        m_progress->setMinimumDuration(300);
        m_progress->setAutoClose(false);
        m_progress->setAutoReset(false);
        connect(m_progress, &QProgressDialog::canceled, this, [this] {
            // cancel() 直接设置原子变量：worker 的导出槽正阻塞在循环里，
            // 队列连接投递的槽要等它返回才会被处理。
            if (m_exportWorker)
                m_exportWorker->cancel();
            if (m_progress) {
                m_progress->setLabelText(QStringLiteral("正在取消…"));
                m_progress->setCancelButton(nullptr);
            }
        });
        m_progress->setValue(0);
    }

    m_history->setExporting(true);
    m_history->setExportStatus(QStringLiteral("正在导出会话 #%1…").arg(sessionId));

    m_exportThread = new QThread(this);
    m_exportThread->setObjectName(QStringLiteral("csv-export"));

    auto *worker = new CsvExportWorker(m_dbPath);
    m_exportWorker = worker;
    worker->moveToThread(m_exportThread);

    const bool utf8Bom = m_settings->utf8Bom(true);
    connect(m_exportThread, &QThread::started, worker,
            [worker, sessionId, path, utf8Bom] {
                worker->exportSession(sessionId, path, utf8Bom);
            });
    connect(worker, &CsvExportWorker::progress, this, &MainWindow::onExportProgress);
    connect(worker, &CsvExportWorker::finished, this, &MainWindow::onExportFinished);
    connect(worker, &CsvExportWorker::logMessage, this, [](const QString &text) {
        qCInfo(lcCsv).noquote() << text;
    });
    connect(m_exportThread, &QThread::finished, this, &MainWindow::onExportThreadFinished);

    m_exportThread->start();
    qCInfo(lcCsv).noquote() << "export started:" << path;
    statusBar()->showMessage(QStringLiteral("正在导出：%1").arg(path));
    return true;
}

void MainWindow::onExportProgress(qint64 done, qint64 total)
{
    if (m_progress) {
        if (total > 0) {
            const int maximum = total > INT_MAX ? INT_MAX : static_cast<int>(total);
            const int value = done > maximum ? maximum : static_cast<int>(done);
            m_progress->setRange(0, maximum);
            m_progress->setValue(value);
            m_progress->setLabelText(
                QStringLiteral("已导出 %1 / %2 行…").arg(done).arg(total));
        } else {
            m_progress->setRange(0, 0); // 总数未知，显示忙碌状态
        }
    }
    if (done > 0 && done % 5000 == 0)
        qCInfo(lcCsv).noquote() << "export progress:" << done << "/" << total;
}

void MainWindow::onExportFinished(bool ok, qint64 rows, const QString &path,
                                  const QString &error)
{
    if (m_progress) {
        m_progress->close();
        m_progress->deleteLater();
        m_progress = nullptr;
    }
    m_history->setExporting(false);

    if (m_shuttingDown) {
        qInfo().noquote() << "[export] finished during shutdown:" << ok << rows << path;
        if (m_exportThread)
            m_exportThread->quit();
        return;
    }

    if (ok) {
        const QString message = QStringLiteral("已导出 %1 行 → %2").arg(rows).arg(path);
        m_history->setExportStatus(message);
        statusBar()->showMessage(message, 8000);
        qCInfo(lcCsv).noquote() << message;
        if (m_exportInteractive)
            QMessageBox::information(this, QStringLiteral("导出完成"), message);
    } else if (error == QStringLiteral("已取消")) {
        m_history->setExportStatus(QStringLiteral("导出已取消，未生成文件"));
        statusBar()->showMessage(QStringLiteral("导出已取消"), 5000);
        qWarning().noquote() << "[export] canceled:" << path;
    } else {
        m_history->setExportStatus(QStringLiteral("导出失败：%1").arg(error));
        statusBar()->showMessage(QStringLiteral("导出失败：%1").arg(error), 8000);
        qWarning().noquote() << "[export] failed:" << error;
        if (m_exportInteractive)
            QMessageBox::warning(this, QStringLiteral("导出失败"), error);
    }

    // worker 已退出导出槽；quit 让线程事件循环结束，随后
    // onExportThreadFinished() 在 GUI 线程回收 worker 和 QThread。
    if (m_exportThread)
        m_exportThread->quit();
}

void MainWindow::onExportThreadFinished()
{
    if (!m_exportThread)
        return;

    // 线程已经结束，worker 不再处理事件，可以安全地在 GUI 线程删除。
    m_history->setExporting(false);
    delete m_exportWorker;
    m_exportWorker = nullptr;

    QThread *thread = m_exportThread;
    m_exportThread = nullptr;
    thread->deleteLater();
}

void MainWindow::stopExportThread()
{
    if (!m_exportThread) {
        if (m_progress) {
            m_progress->close();
            m_progress->deleteLater();
            m_progress = nullptr;
        }
        return;
    }

    if (m_exportWorker)
        m_exportWorker->cancel();

    m_exportThread->quit();
    if (!m_exportThread->wait(3000)) {
        qWarning().noquote() << "[export] worker did not stop within 3 s; terminating";
        m_exportThread->terminate();
        m_exportThread->wait(1000);
    }

    // 关窗路径上不再处理后续信号。
    disconnect(m_exportThread, nullptr, this, nullptr);
    delete m_exportWorker;
    m_exportWorker = nullptr;

    QThread *thread = m_exportThread;
    m_exportThread = nullptr;
    delete thread;

    if (m_progress) {
        m_progress->close();
        m_progress->deleteLater();
        m_progress = nullptr;
    }
    m_history->setExporting(false);
}

void MainWindow::updateLabels()
{
    m_workerLabel->setText(QStringLiteral("帧 %1 | 样本 %2 | 重连 %3 | 界面已收 %4")
                               .arg(m_frames)
                               .arg(m_samples)
                               .arg(m_reconnects)
                               .arg(m_consumedSamples));

    m_queueLabel->setText(QStringLiteral("队列 %1/%2 | 峰值 %3 | 丢弃 %4")
                              .arg(m_queue.size())
                              .arg(m_queue.capacity())
                              .arg(m_queue.highWatermark())
                              .arg(m_queue.droppedCount()));

    const QString session = m_sessionId > 0
                                ? QStringLiteral("#%1").arg(m_sessionId)
                                : QStringLiteral("-");
    m_dbLabel->setText(QStringLiteral("会话 %1 | 写入 %2 行 | 提交 %3 次 | 最后 %4 ms | 待写 %5")
                           .arg(session)
                           .arg(m_dbRows)
                           .arg(m_dbCommits)
                           .arg(static_cast<double>(m_dbLastCommitUs) / 1000.0, 0, 'f', 2)
                           .arg(m_dbPending));
}

void MainWindow::shutdownAll()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;

    if (m_drainTimer)
        m_drainTimer->stop();

    // 0) 先停止 CSV 导出并取消未完成的临时文件，避免它和 DB 收尾互相等待。
    stopExportThread();

    // 1) 先停生产者：不再有新样本进入队列。
    if (m_thread) {
        if (m_worker)
            QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
        else
            m_thread->quit();

        if (!m_thread->wait(3000)) {
            qWarning().noquote()
                << "[gui] worker did not stop within 3 s; forcing quit";
            m_thread->quit();
            if (!m_thread->wait(1000)) {
                qWarning().noquote() << "[gui] terminating worker thread as last resort";
                m_thread->terminate();
                m_thread->wait(1000);
            }
        }
        m_worker = nullptr;
        delete m_thread;
        m_thread = nullptr;
    }

    // 2) 最后一次排空 + 结束会话（通过队列连接发给 writer）。
    if (m_sessionActive) {
        drainQueue();
        m_sessionActive = false;
        emit endSessionRequested();
    }

    // 3) 停 DB 线程：shutdown() 内部 flush、关会话、close、removeDatabase、quit。
    if (m_dbThread) {
        if (m_writer)
            QMetaObject::invokeMethod(m_writer, &DbWriter::shutdown, Qt::QueuedConnection);

        if (!m_dbThread->wait(5000)) {
            qWarning().noquote() << "[gui] db writer did not stop within 5 s; forcing quit";
            m_dbThread->quit();
            if (!m_dbThread->wait(1000)) {
                qWarning().noquote() << "[gui] terminating db thread as last resort";
                m_dbThread->terminate();
                m_dbThread->wait(1000);
            }
        }
        m_writer = nullptr;
        delete m_dbThread;
        m_dbThread = nullptr;
    }

    qInfo().noquote()
        << QStringLiteral("[gui] shutdown complete: gui_consumed=%1 queue_dropped=%2 "
                          "queue_peak=%3 db_rows=%4 db_commits=%5")
               .arg(m_consumedSamples)
               .arg(m_queue.droppedCount())
               .arg(m_queue.highWatermark())
               .arg(m_dbRows)
               .arg(m_dbCommits);
}

void MainWindow::saveWindowSettings()
{
    m_settings->setWindowGeometry(saveGeometry());
    m_settings->setWindowState(saveState());
    QString error;
    if (!m_settings->sync(&error))
        qWarning().noquote() << "[settings]" << error;
    else
        qInfo().noquote() << "[settings] saved:" << m_settings->fileName();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveWindowSettings();
    shutdownAll();
    QMainWindow::closeEvent(event);
}