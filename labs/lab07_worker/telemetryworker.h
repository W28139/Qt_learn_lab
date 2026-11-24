#pragma once

#include "boundedqueue.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QtGlobal>

class QTcpSocket;
class QTimer;
class StreamDecoder;
struct DecodeResult;

// 运行在工作线程里的网络 worker
//
// 构造发生在 GUI 线程，但只移动对象本身；socket、timer、decoder 都在
// start() 槽里创建，确保它们的线程亲和性也是工作线程。
class TelemetryWorker final : public QObject
{
    Q_OBJECT

public:
    // 这个 worker线程 当前所处的连接生命周期阶段
    enum class State {
        Idle = 0,        // 初始状态
        Connecting,      // 正在建立连接
        Connected,       // 已连接，正常收数据
        Reconnecting,    // 断线后等待重连
        Failed,          // 不可恢复（重试耗尽 / 协议错误）
        Stopped,         // stop() 已完成
    };

    static QString stateName(State state);  // 枚举转可读文本，用于日志/界面

    TelemetryWorker(QString host, quint16 port, BoundedBatchQueue *queue,
                    QObject *parent = nullptr);
    ~TelemetryWorker() override;

public slots:
    // 必须通过队列连接调用（或由 QThread::started 触发），否则会在错误线程创建 socket。
    void start();
    void stop();

signals:
    // state 用 int 传递，避免枚举跨线程时还要关心元类型注册。
    void stateChanged(int state, const QString &detail);
    void logMessage(const QString &text);
    // 统计信号有节流（250 ms），不是每帧发一次，避免高频数据把 GUI 事件队列打满。
    void statsChanged(quint64 frames, quint64 samples, quint64 reconnects);
    // 报告 worker 真实线程 id，用于在界面上和 GUI 线程 id 对照。
    void workerThreadId(quintptr threadId);

private:
    void connectNow();          // 发起一次连接（初次或重连都走它）
    void onConnected();         // 连接成功：更新状态、清空重连计数
    void onReadyRead();         // 收到字节：喂给 decoder，循环取帧
    void onDisconnected();      // 断线：正常 stop 则忽略，否则安排重连
    void onSocketError();       // socket 错误：记录原因并可能重连
    void scheduleReconnect(const QString &reason);  // 递增退避后安排重连
    bool handleFrame(const DecodeResult &frame);    // 处理一帧；false 表示致命协议错误
    void reportFatalProtocolError(const QString &detail);  // 进入 Failed，停止一切重连
    void emitStatsIfDue();      // 节流发送 statsChanged（≥250 ms 一次）
    void teardown();            // 拆除 socket/timer/decoder，释放资源

    void setState(State state, const QString &detail);  // 统一改状态并 emit

    QString m_host;             // 目标主机
    quint16 m_port = 0;         // 目标端口
    BoundedBatchQueue *m_queue = nullptr; // 不拥有队列，生命周期归 MainWindow

    // 这些对象全部在 start() 里创建，因此亲和性属于工作线程。
    // socket/timer 的 parent 是 this，但 teardown() 仍显式删除以便立即释放；
    // decoder 不是 QObject，没有 parent，必须手动 delete。
    QTcpSocket *m_socket = nullptr;
    QTimer *m_reconnectTimer = nullptr; // 延迟触发重连的 QTimer——断线后不立刻重连，而是等一会儿（退避）再发起
    StreamDecoder *m_decoder = nullptr;
    // 给统计信号做节流的计时器——保证 statsChanged 信号最多每 250 ms 发一次，而不是每收到一帧就发一次。
    QElapsedTimer m_statsClock;

    quint64 m_frames = 0;           // 累计解析出的帧数
    quint64 m_samples = 0;          // 累计样本数
    quint64 m_reconnects = 0;       // 累计重连次数
    quint64 m_protocolErrors = 0;   // 累计协议错误数

    int m_attempt = 0;       // 已安排的重连次数；连接成功后清零
    bool m_started = false;  // start() 是否已执行
    bool m_stopping = false; // stop() 进行中：忽略断开/错误，不再安排重连
    bool m_fatal = false;    // 不可恢复错误：禁止任何重连
};