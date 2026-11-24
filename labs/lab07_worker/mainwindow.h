#pragma once

#include "boundedqueue.h"

#include <QMainWindow>
#include <QString>

// 前置声明：只用作指针成员，减少头文件依赖
class QLabel;
class QPushButton;
class QTableView;
class QThread;
class QTimer;
class TelemetryModel;
class TelemetryWorker;

// 一个最小但完整的"GUI + worker 线程"窗口：
// worker 在工作线程里收包、解包、解码，把批次推进线程安全队列；
// GUI 线程只做一件事：按固定频率取队列、更新模型和控件。
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    // host/port 是连接目标；embedded 表示 server 是否在本进程内
    MainWindow(QString host, quint16 port, bool embedded, QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;  // 关窗前优雅停掉 worker

private slots:
    void startWorker();                                        // 启动 worker 线程
    void requestStop();                                        // 请求停止 worker
    void drainQueue();                                         // 定时从队列取数据刷模型
    void clearAll();                                           // 清空模型与统计显示
    void onWorkerState(int state, const QString &detail);      // 更新状态标签
    void onWorkerStats(quint64 frames, quint64 samples, quint64 reconnects); // 更新统计标签
    void onWorkerLog(const QString &text);                     // 追加日志
    void onWorkerThreadId(quintptr threadId);                  // 记录 worker 线程 ID
    void onThreadFinished();                                   // 线程结束后清理

private:
    void buildUi();          // 创建控件、布局、连接
    void updateLabels();     // 刷新各标签文本
    void shutdownWorker();   // 停线程并释放 worker（优雅退出）

    QString m_host;          // 连接目标主机
    quint16 m_port = 0;      // 连接目标端口
    bool m_embedded = false; // 是否嵌入式模式（server 在本进程内）

    BoundedBatchQueue m_queue{64};  // worker -> GUI 的线程安全队列，容量 64
    TelemetryModel *m_model = nullptr;  // 表格数据模型（环形缓冲）

    QTableView *m_view = nullptr;       // 样本表格视图
    QPushButton *m_startButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QLabel *m_stateLabel = nullptr;     // 显示 worker 状态
    QLabel *m_statsLabel = nullptr;     // 显示帧/样本/重连统计
    QLabel *m_queueLabel = nullptr;     // 显示队列深度/丢弃数
    QLabel *m_threadLabel = nullptr;    // 显示 GUI/worker 线程 ID
    QTimer *m_drainTimer = nullptr;     // 定时触发 drainQueue()

    QThread *m_thread = nullptr;        // worker 所在线程
    TelemetryWorker *m_worker = nullptr; // 网络 worker（跑在 m_thread 里）

    quintptr m_guiThreadId = 0;         // GUI 线程 ID（对照用）
    quintptr m_workerThreadId = 0;      // worker 线程 ID（对照用）
    quint64 m_frames = 0;               // 累计帧数（来自 worker 统计）
    quint64 m_samples = 0;              // 累计样本数（来自 worker 统计）
    quint64 m_reconnects = 0;           // 累计重连次数
    quint64 m_consumedSamples = 0;      // GUI 侧已消费的样本数
};