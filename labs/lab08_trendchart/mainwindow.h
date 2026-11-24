#pragma once

#include "boundedqueue.h"

#include <QMainWindow>
#include <QString>

class ChartWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QThread;
class QTimer;
class TelemetryWorker;

// Lab 08 的主窗口：保留单元 07 的 worker + 有界队列，只把“表格”换成
// “自绘趋势图”。GUI 线程每 40 ms 取一次队列，把样本追加给 ChartWidget。
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QString host, quint16 port, bool embedded, QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void startWorker();
    void requestStop();
    void drainQueue();
    void clearAll();
    void togglePause(bool paused);
    void onWorkerState(int state, const QString &detail);
    void onWorkerStats(quint64 frames, quint64 samples, quint64 reconnects);
    void onWorkerLog(const QString &text);
    void onThreadFinished();

private:
    void buildUi();
    void updateLabels();
    void shutdownWorker();

    QString m_host;
    quint16 m_port = 0;
    bool m_embedded = false;

    BoundedBatchQueue m_queue{64};

    ChartWidget *m_chart = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QPushButton *m_pauseButton = nullptr;
    QComboBox *m_windowCombo = nullptr;
    QCheckBox *m_autoScaleBox = nullptr;
    QCheckBox *m_gridBox = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_statsLabel = nullptr;
    QLabel *m_queueLabel = nullptr;
    QLabel *m_chartLabel = nullptr;
    QTimer *m_drainTimer = nullptr;

    QThread *m_thread = nullptr;
    TelemetryWorker *m_worker = nullptr;

    quint64 m_frames = 0;
    quint64 m_samples = 0;
    quint64 m_reconnects = 0;
    quint64 m_consumedSamples = 0;
};