#pragma once

#include "boundedqueue.h"

#include <QMainWindow>
#include <QString>

class QLabel;
class QPushButton;
class QTabWidget;
class QTableView;
class QThread;
class QTimer;
class DbWriter;
class HistoryPanel;
class LiveSampleModel;
class TelemetryWorker;

// Lab 09 主窗口：数据链路沿用单元 07/08，但终点从“画图”换成
// “实时表 + 落盘 + 历史分页查询”。
//
// 三个线程各管一件事：
// - GUI 线程：取队列、更新实时表、发查询；
// - 网络 worker 线程：收包、解码、入队；
// - DB writer 线程：批量事务写 SQLite。
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QString dbPath, QString host, quint16 port, bool embedded,
               QWidget *parent = nullptr);
    ~MainWindow() override;

public slots:
    void showHistoryTab();
    // 等价于点击“停止”，供命令行定时停止和无头验证使用。
    void stopCapture();

signals:
    // 这三个信号都通过队列连接发给 DB writer，保证调用顺序与 GUI 发出的顺序一致。
    void beginSessionRequested();
    void persistSamples(const QList<TelemetrySample> &samples);
    void endSessionRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onDbOpened(const QString &path, const QString &error);
    void onDbSessionStarted(qint64 sessionId, qint64 startedMs);
    void onDbSessionFinished(qint64 sessionId, qint64 sampleCount);
    void onDbStats(quint64 rowsWritten, quint64 commits, qint64 lastCommitUs,
                   qint64 pendingRows);
    void onDbError(const QString &text);
    void onDbCleared();

    void startWorker();
    void requestStop();
    void onThreadFinished();
    void drainQueue();
    void clearHistory();

    void onWorkerState(int state, const QString &detail);
    void onWorkerStats(quint64 frames, quint64 samples, quint64 reconnects);
    void onWorkerLog(const QString &text);

private:
    void buildUi();
    void updateLabels();
    void shutdownAll();

    QString m_dbPath;
    QString m_host;
    quint16 m_port = 0;
    bool m_embedded = false;

    BoundedBatchQueue m_queue{64};

    QTabWidget *m_tabs = nullptr;
    QTableView *m_liveTable = nullptr;
    LiveSampleModel *m_liveModel = nullptr;
    HistoryPanel *m_history = nullptr;

    QPushButton *m_startButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_clearButton = nullptr;

    QLabel *m_stateLabel = nullptr;
    QLabel *m_workerLabel = nullptr;
    QLabel *m_queueLabel = nullptr;
    QLabel *m_dbLabel = nullptr;

    QTimer *m_drainTimer = nullptr;

    // 网络 worker
    QThread *m_thread = nullptr;
    TelemetryWorker *m_worker = nullptr;

    // 数据库 writer
    QThread *m_dbThread = nullptr;
    DbWriter *m_writer = nullptr;

    quint64 m_frames = 0;
    quint64 m_samples = 0;
    quint64 m_reconnects = 0;
    quint64 m_consumedSamples = 0;

    quint64 m_dbRows = 0;
    quint64 m_dbCommits = 0;
    qint64 m_dbLastCommitUs = 0;
    qint64 m_dbPending = 0;

    qint64 m_sessionId = 0;
    bool m_sessionActive = false;
    bool m_dbOk = false;
    bool m_shuttingDown = false;
};