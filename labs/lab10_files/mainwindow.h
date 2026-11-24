#pragma once

#include "apppaths.h"
#include "boundedqueue.h"

#include <QMainWindow>
#include <QString>

class AppSettings;
class CsvExportWorker;
class QLabel;
class QProgressDialog;
class QPushButton;
class QTabWidget;
class QTableView;
class QThread;
class QTimer;
class DbWriter;
class HistoryPanel;
class LiveSampleModel;
class TelemetryWorker;

// Lab 10 主窗口：数据链路沿用单元 07/08，终点是“实时表 + 落盘 + 历史查询”；
// 本单元额外加入标准路径、QSettings 配置、CSV 导出和文件日志。
//
// 线程：
// - GUI 线程：取队列、更新实时表、发查询、写配置；
// - 网络 worker 线程：收包、解码、入队；
// - DB writer 线程：批量事务写 SQLite；
// - CSV 导出线程：只读连接 + 流式写文件，导出期间 GUI 不阻塞。
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(const apppaths::Paths &paths, QString host, quint16 port, bool embedded,
               AppSettings &settings, QWidget *parent = nullptr);
    ~MainWindow() override;

    qint64 currentSessionId() const { return m_sessionId; }

public slots:
    void showHistoryTab();
    // 等价于点击“停止”，供命令行定时停止和无头验证使用。
    void stopCapture();
    void showSettingsDialog();
    // 弹出文件选择框，导出历史页当前选中的会话。
    void exportSelectedSession();
    // 无对话框：按默认文件名导出，供 --export-after-seconds 验证使用。
    void exportSelectedSessionAuto();

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

    void onHistoryExportRequested(qint64 sessionId);
    void onExportProgress(qint64 done, qint64 total);
    void onExportFinished(bool ok, qint64 rows, const QString &path, const QString &error);
    void onExportThreadFinished();

private:
    void buildUi();
    void updateLabels();
    void shutdownAll();
    void saveWindowSettings();
    bool startExport(qint64 sessionId, const QString &path, bool interactive);
    void stopExportThread();
    QString defaultExportPath(qint64 sessionId) const;
    void applyLogLevel(const QString &level);

    apppaths::Paths m_paths;
    AppSettings *m_settings = nullptr;

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
    QPushButton *m_settingsButton = nullptr;
    QPushButton *m_openLogButton = nullptr;

    QLabel *m_stateLabel = nullptr;
    QLabel *m_workerLabel = nullptr;
    QLabel *m_queueLabel = nullptr;
    QLabel *m_dbLabel = nullptr;
    QLabel *m_logLabel = nullptr;

    QTimer *m_drainTimer = nullptr;

    // 网络 worker
    QThread *m_thread = nullptr;
    TelemetryWorker *m_worker = nullptr;

    // 数据库 writer
    QThread *m_dbThread = nullptr;
    DbWriter *m_writer = nullptr;

    // CSV 导出线程（每次导出临时创建）
    QThread *m_exportThread = nullptr;
    CsvExportWorker *m_exportWorker = nullptr;
    QProgressDialog *m_progress = nullptr;
    bool m_exportInteractive = false;

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