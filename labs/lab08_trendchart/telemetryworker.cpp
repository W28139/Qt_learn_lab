#include "telemetryworker.h"

#include "protocol.h"
#include "streamdecoder.h"

#include <QAbstractSocket>
#include <QDateTime>
#include <QMetaObject>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <utility>

namespace {

constexpr int kMaxReconnectAttempts = 3;
constexpr int kStatsIntervalMs = 250;

} // namespace

TelemetryWorker::TelemetryWorker(QString host, quint16 port, BoundedBatchQueue *queue,
                                 QObject *parent)
    : QObject(parent)
    , m_host(std::move(host))
    , m_port(port)
    , m_queue(queue)
{
    // 构造函数在 GUI 线程执行：这里只保存参数，绝不能创建 socket/timer/decoder。
    // 对象的线程亲和性在创建时确定，真正的网络对象要等到 start() 在工作线程执行时再建。
}

TelemetryWorker::~TelemetryWorker()
{
    // 正常路径上 stop() 已经清理过；这里兜底，保证析构时不再持有活动 socket。
    teardown();
}

QString TelemetryWorker::stateName(State state)
{
    switch (state) {
    case State::Idle:
        return QStringLiteral("待机");
    case State::Connecting:
        return QStringLiteral("连接中");
    case State::Connected:
        return QStringLiteral("已连接");
    case State::Reconnecting:
        return QStringLiteral("重连中");
    case State::Failed:
        return QStringLiteral("失败");
    case State::Stopped:
        return QStringLiteral("已停止");
    }
    return QStringLiteral("未知");
}

void TelemetryWorker::start()
{
    if (m_started)
        return;

    m_started = true;
    // 每次启动都从干净状态开始，统计也归零（与界面的“本轮运行”语义一致）。
    m_stopping = false;
    m_fatal = false;
    m_attempt = 0;
    m_frames = 0;
    m_samples = 0;
    m_reconnects = 0;
    m_protocolErrors = 0;

    // 关键：QObject 的线程亲和性在创建时确定。
    // start() 已经在工作线程执行，所以在这里 new 出来的 socket/timer 都属于工作线程。
    m_decoder = new StreamDecoder;

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true); // 每次退避只触发一次，由 timeout 重新发起连接
    connect(m_reconnectTimer, &QTimer::timeout, this, &TelemetryWorker::connectNow);

    m_socket = new QTcpSocket(this);
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(m_socket, &QTcpSocket::connected, this, &TelemetryWorker::onConnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &TelemetryWorker::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &TelemetryWorker::onDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &TelemetryWorker::onSocketError);

    m_statsClock.start();
    // 取出"当前正在执行这行代码的线程"的底层线程 ID，把它转成整数，通过 workerThreadId 信号发出去。
    emit workerThreadId(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    emit logMessage(QStringLiteral("worker started, connecting to %1:%2")
                        .arg(m_host)
                        .arg(m_port));

    connectNow();
}

void TelemetryWorker::stop()
{
    // 幂等：从未启动或已经清理过，直接报告 Stopped，不重复 teardown。
    if (!m_started && !m_socket && !m_reconnectTimer) {
        setState(State::Stopped, QStringLiteral("stopped"));
        return;
    }

    m_stopping = true;
    // teardown() 删除的都是本线程（worker 线程）创建的对象，可以直接 delete。
    teardown();
    m_started = false;
    emit statsChanged(m_frames, m_samples, m_reconnects);
    emit logMessage(QStringLiteral("worker stopped by request"));
    setState(State::Stopped, QStringLiteral("stopped"));

    // 关键：由 stop() 自己结束事件循环，而不是让调用方在发出 stop 后立刻 quit()。
    // QThread::quit() 可能让事件循环先退出，导致前面排队的 stop() 事件根本来不及执行。
    QThread *workerThread = thread();
    if (workerThread && QThread::currentThread() == workerThread)
        workerThread->quit();
}

void TelemetryWorker::connectNow()
{
    if (!m_started || m_stopping || m_fatal || !m_socket || !m_decoder)
        return;

    // 新连接一定是新的字节流：旧连接的半帧必须清掉，否则会把新数据拼进旧帧。
    m_decoder->clear();

    // 主动 abort 旧连接，丢弃残留状态，从干净状态重新开始。
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();

    setState(State::Connecting,
             QStringLiteral("connecting to %1:%2").arg(m_host).arg(m_port));
    m_socket->connectToHost(m_host, m_port);
}

void TelemetryWorker::onConnected()
{
    // 成功连上就重置退避计数：下一次断线重新从 1 s 开始。
    m_attempt = 0;
    setState(State::Connected,
             QStringLiteral("connected to %1:%2")
                 .arg(m_socket->peerAddress().toString())
                 .arg(m_socket->peerPort()));
    emit logMessage(QStringLiteral("connected, peer=%1:%2")
                        .arg(m_socket->peerAddress().toString())
                        .arg(m_socket->peerPort()));

    // 心跳只是为了让服务端有东西可解析；这个 Lab 的 mock server 会忽略它。
    const EncodeResult heartbeat = proto::encodeFrame(
        MessageType::Heartbeat, 1,
        static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()), QByteArray());
    if (heartbeat.ok())
        m_socket->write(heartbeat.frame);
}

void TelemetryWorker::onReadyRead()
{
    if (!m_decoder || !m_socket)
        return;

    // 一次 readyRead 可能带来任意多帧：先把当前可读字节全部交给解包器，
    // 再循环取帧。半帧时解包器保留剩余字节，等下一次 readyRead 继续拼。
    m_decoder->append(m_socket->readAll());

    for (;;) {
        const StreamResult result = m_decoder->nextFrame();
        if (result.status == StreamStatus::NeedMoreData) // 半帧，等更多数据
            break;

        // 协议错误对当前连接是致命的：报 Failed 并停止消费，不重连。
        if (result.status == StreamStatus::ProtocolError) {
            ++m_protocolErrors;
            reportFatalProtocolError(
                QStringLiteral("%1 (%2)")
                    .arg(proto::codecErrorText(result.frame.error), result.detail));
            return;
        }

        if (!handleFrame(result.frame))
            return;
    }

    emitStatsIfDue();
}

bool TelemetryWorker::handleFrame(const DecodeResult &frame)
{
    // 本 Lab 只处理遥测批次；Heartbeat/Ack 等类型直接忽略。
    if (frame.header.type != MessageType::TelemetryBatch)
        return true;

    QString deviceId;
    QList<TelemetrySample> samples;
    CodecError error = CodecError::None;
    QString detail;
    if (!proto::decodeTelemetryPayload(frame.payload, &deviceId, &samples, &error,
                                       &detail)) {
        ++m_protocolErrors;
        reportFatalProtocolError(
            QStringLiteral("%1 (%2)").arg(proto::codecErrorText(error), detail));
        return false;
    }

    TelemetryBatch batch;
    batch.sequence = frame.header.sequence;
    batch.deviceId = deviceId;
    batch.samples = samples; // 按值拷入批次：入队后 worker 不再引用这些数据

    // 这里是跨线程交接点：worker 线程生产，GUI 线程消费。
    // push() 内部加锁；队列满时丢最旧一批，worker 不会阻塞。
    const bool keptEverything = m_queue->push(batch);
    if (!keptEverything) {
        emit logMessage(QStringLiteral("queue full, dropped oldest batch (seq=%1)")
                            .arg(frame.header.sequence));
    }

    ++m_frames;
    m_samples += static_cast<quint64>(samples.size());
    return true;
}

void TelemetryWorker::onDisconnected()
{
    if (m_stopping || m_fatal)
        return;

    emit logMessage(QStringLiteral("disconnected"));
    scheduleReconnect(QStringLiteral("remote closed the connection"));
}

void TelemetryWorker::onSocketError()
{
    if (!m_socket)
        return;

    const QString reason = m_socket->errorString();
    emit logMessage(QStringLiteral("socket error: %1").arg(reason));

    if (m_stopping || m_fatal)
        return;
    scheduleReconnect(reason);
}

void TelemetryWorker::scheduleReconnect(const QString &reason)
{
    if (m_stopping || m_fatal || !m_reconnectTimer)
        return;

    // errorOccurred 和 disconnected 可能先后都到；定时器已在跑就不要再安排。
    if (m_reconnectTimer->isActive())
        return;

    if (m_attempt >= kMaxReconnectAttempts) {
        // 重试次数用尽：切断连接并进入 Failed，不再安排任何定时器。
        if (m_socket)
            m_socket->abort();
        setState(State::Failed, QStringLiteral("%1; giving up after %2 attempts")
                                    .arg(reason)
                                    .arg(kMaxReconnectAttempts));
        emit logMessage(QStringLiteral("reconnect attempts exhausted"));
        emit statsChanged(m_frames, m_samples, m_reconnects);
        return;
    }

    const int delayMs = 1000 << m_attempt; // 指数退避：1 s、2 s、4 s
    ++m_attempt;
    ++m_reconnects;
    setState(State::Reconnecting,
             QStringLiteral("%1; retry in %2 ms (attempt %3/%4)")
                 .arg(reason)
                 .arg(delayMs)
                 .arg(m_attempt)
                 .arg(kMaxReconnectAttempts));
    emit logMessage(QStringLiteral("%1; reconnecting in %2 ms (attempt %3/%4)")
                        .arg(reason)
                        .arg(delayMs)
                        .arg(m_attempt)
                        .arg(kMaxReconnectAttempts));
    m_reconnectTimer->start(delayMs);
}

void TelemetryWorker::reportFatalProtocolError(const QString &detail)
{
    // 先置 fatal，再 abort；这样即便 abort 间接触发 disconnected，
    // onDisconnected/onSocketError 也会因为 fatal 直接返回，不会安排重连。
    m_fatal = true;
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    if (m_socket) {
        // abort() 会触发 disconnected；m_fatal 已经置位，那里不会安排重连。
        m_socket->abort();
    }
    setState(State::Failed,
             QStringLiteral("protocol error: %1; not reconnecting").arg(detail));
    emit logMessage(QStringLiteral("protocol error: %1; not reconnecting").arg(detail));
    emit statsChanged(m_frames, m_samples, m_reconnects);
}

void TelemetryWorker::emitStatsIfDue()
{
    // 节流：解包循环结束后才检查，最多每 250 ms 发一次跨线程信号。
    // 1000 Hz 的高频数据如果每帧发信号，会把 GUI 事件队列打满。
    if (!m_statsClock.isValid() || m_statsClock.elapsed() < kStatsIntervalMs)
        return;

    m_statsClock.restart();
    emit statsChanged(m_frames, m_samples, m_reconnects);
}

void TelemetryWorker::teardown()
{
    // 只应在 worker 线程调用（stop() 槽或 worker 线程里的 deleteLater 析构），
    // 所以这里直接 delete 是安全的；停止后对象立即释放，不留后台活动。
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
        delete m_reconnectTimer;
        m_reconnectTimer = nullptr;
    }
    if (m_socket) {
        m_socket->abort();
        delete m_socket;
        m_socket = nullptr;
    }
    delete m_decoder;
    m_decoder = nullptr;
}

void TelemetryWorker::setState(State state, const QString &detail)
{
    // 统一从 worker 线程发出；GUI 侧用 AutoConnection 会排成队列连接，
    // 槽函数自然在 GUI 线程执行，界面更新是安全的。
    emit stateChanged(static_cast<int>(state), detail);
}