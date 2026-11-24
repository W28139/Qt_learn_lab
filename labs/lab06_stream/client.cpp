#include "client.h"

#include <QDateTime>

#include <utility>

// 构造：连接 socket 与定时器信号，配置重连/连接超时定时器（均单次触发）
FrameClient::FrameClient(QString host, quint16 port, int targetFrames, QObject *parent)
    : QObject(parent)
    , m_host(std::move(host))
    , m_port(port)
    , m_targetFrames(targetFrames)
{
    connect(&m_socket, &QTcpSocket::connected, this, &FrameClient::onConnected);
    connect(&m_socket, &QTcpSocket::readyRead, this, &FrameClient::onReadyRead);
    connect(&m_socket, &QTcpSocket::disconnected, this, &FrameClient::onDisconnected);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &FrameClient::onSocketError);

    // 重连延时到点后执行一次连接尝试
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &FrameClient::connectAgain);

    // 连接超时：5s 内仍处于连接中就 abort 并安排重连
    m_connectTimer.setSingleShot(true);
    connect(&m_connectTimer, &QTimer::timeout, this, [this] {
        if (m_socket.state() == QAbstractSocket::ConnectingState) {
            m_socket.abort();
            scheduleReconnect(QStringLiteral("connect timed out"));
        }
    });
}

// 启动：打印日志、开启连接超时定时器、发起 TCP 连接
void FrameClient::start()
{
    emit logMessage(QStringLiteral("connecting to %1:%2").arg(m_host).arg(m_port));
    m_connectTimer.start(5000);
    m_socket.connectToHost(m_host, m_port);
}

// 用户主动停止：停止所有定时器、断开连接、发出 finished（不再重连）
void FrameClient::stop()
{
    if (m_done)
        return;
    m_userStopped = true;
    m_done = true;
    m_connectTimer.stop();
    m_reconnectTimer.stop();
    m_socket.abort();
    emitFinishedOnce();
}

// 连接成功：停超时定时器、重置重连状态、清理解码器，并发送心跳帧
void FrameClient::onConnected()
{
    m_connectTimer.stop();
    m_attempt = 0;
    m_reconnectScheduled = false;
    m_decoder.clear();

    emit logMessage(QStringLiteral("connected, sending heartbeat"));

    // 编码并发送 Heartbeat 帧（seq=1，时间戳为负载）
    const EncodeResult hello = proto::encodeFrame(
        MessageType::Heartbeat, 1,
        static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()), {});
    if (!hello.ok()) {
        fail(QStringLiteral("cannot encode heartbeat: %1").arg(hello.detail));
        return;
    }
    m_socket.write(hello.frame);
}

// 有数据可读：读入解码器并循环取帧处理
void FrameClient::onReadyRead()
{
    m_decoder.append(m_socket.readAll());

    for (;;) {
        const StreamResult result = m_decoder.nextFrame();
        if (result.status == StreamStatus::NeedMoreData)  // 数据不足，等下次
            break;

        // 协议错误：不可恢复，直接失败
        if (result.status == StreamStatus::ProtocolError) {
            fail(QStringLiteral("protocol error: %1").arg(result.detail));
            m_socket.abort();
            return;
        }

        const auto &header = result.frame.header;
        // Ack / 异常 Heartbeat：仅记录日志，不计数
        if (header.type == MessageType::Ack) {
            emit logMessage(QStringLiteral("server ack seq=%1").arg(header.sequence));
            continue;
        }
        if (header.type == MessageType::Heartbeat) {
            emit logMessage(QStringLiteral("unexpected heartbeat from server"));
            continue;
        }

        // 解析遥测负载
        QString deviceId;
        QList<TelemetrySample> samples;
        CodecError error = CodecError::None;
        QString detail;
        if (!proto::decodeTelemetryPayload(result.frame.payload, &deviceId, &samples,
                                           &error, &detail)) {
            fail(QStringLiteral("payload error: %1").arg(detail));
            m_socket.abort();
            return;
        }

        ++m_receivedFrames;
        m_receivedSamples += samples.size();

        // 每 10 帧打印一次进度
        if (m_receivedFrames % 10 == 0) {
            emit logMessage(QStringLiteral("frames=%1 samples=%2 lastSeq=%3 device=%4")
                                .arg(m_receivedFrames)
                                .arg(m_receivedSamples)
                                .arg(header.sequence)
                                .arg(deviceId));
        }

        // 达到目标帧数：停止并结束
        if (m_targetFrames > 0 && m_receivedFrames >= m_targetFrames) {
            emit logMessage(QStringLiteral("target reached: %1 frames, %2 samples, discarded=%3")
                                .arg(m_receivedFrames)
                                .arg(m_receivedSamples)
                                .arg(m_decoder.discardedBytes()));
            m_done = true;
            m_connectTimer.stop();
            m_reconnectTimer.stop();
            emitFinishedOnce();
            m_socket.disconnectFromHost();
            return;
        }
    }
}

// 连接断开：非用户主动停止且未完成时，安排重连
void FrameClient::onDisconnected()
{
    if (m_done || m_userStopped)
        return;
    scheduleReconnect(QStringLiteral("connection closed"));
}

// socket 错误：记录日志后安排重连
void FrameClient::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    emit logMessage(QStringLiteral("socket error: %1").arg(m_socket.errorString()));
    if (m_done || m_userStopped)
        return;
    scheduleReconnect(m_socket.errorString());
}

// 安排重连：最多 3 次，指数退避（1s, 2s, 4s）；超过则判定为致命错误
void FrameClient::scheduleReconnect(const QString &reason)
{
    if (m_done || m_userStopped || m_reconnectScheduled)
        return;

    if (m_attempt >= 3) {
        fail(QStringLiteral("giving up after 3 retries: %1").arg(reason));
        return;
    }

    const int delayMs = 1000 << m_attempt;   // 指数退避
    ++m_attempt;
    m_reconnectScheduled = true;

    emit logMessage(QStringLiteral("%1; reconnecting in %2 ms (attempt %3/3)")
                        .arg(reason)
                        .arg(delayMs)
                        .arg(m_attempt));
    m_reconnectTimer.start(delayMs);
}

// 执行一次连接尝试：重置状态、清解码器、启动超时定时器并 connectToHost
void FrameClient::connectAgain()
{
    if (m_done || m_userStopped)
        return;

    m_reconnectScheduled = false;
    m_socket.abort();
    m_decoder.clear();
    m_connectTimer.start(5000);
    m_socket.connectToHost(m_host, m_port);
}

// 致命错误：标记失败、停止定时器、记录错误并结束
void FrameClient::fail(const QString &reason)
{
    m_fatal = true;
    m_done = true;
    m_lastError = reason;
    m_connectTimer.stop();
    m_reconnectTimer.stop();
    emit logMessage(QStringLiteral("fatal: %1").arg(reason));
    emitFinishedOnce();
}

// 保证 finished 信号只发出一次
void FrameClient::emitFinishedOnce()
{
    if (m_finishedEmitted)
        return;
    m_finishedEmitted = true;
    emit finished();
}