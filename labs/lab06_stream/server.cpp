#include "server.h"

#include <QDateTime>
#include <QHostAddress>
#include <QList>

#include <cmath>

namespace {
// 限制单个客户端写入缓冲区的最大字节数，防止慢客户端撑爆内存
constexpr qint64 kMaxQueuedBytes = 256 * 1024;
}

FrameServer::FrameServer(QObject *parent)
    : QObject(parent)
{
    // 当有新客户端连接时，触发 handleNewConnections
    connect(&m_server, &QTcpServer::newConnection, this, &FrameServer::handleNewConnections);
}

FrameServer::~FrameServer()
{
    // 析构时关闭所有连接
    stopAll();
}

bool FrameServer::listen(quint16 port, QString *error)
{
    // 如果已经在监听，直接返回成功
    if (m_server.isListening())
        return true;

    // 尝试在本机(127.0.0.1)监听指定端口
    if (!m_server.listen(QHostAddress::LocalHost, port)) {
        if (error)
            *error = m_server.errorString();
        return false;
    }

    // 打印监听成功的日志
    emit logMessage(QStringLiteral("listening on 127.0.0.1:%1").arg(m_server.serverPort()));
    return true;
}

void FrameServer::stopAll()
{
    // 复制一份 key 列表，避免遍历时修改 map 导致崩溃
    const auto sockets = m_clients.keys();
    for (QTcpSocket *socket : sockets) {
        socket->abort();          // 强制断开连接
        removeClient(socket);     // 清理客户端状态
    }
    m_server.close();             // 停止监听
}

quint16 FrameServer::serverPort() const
{
    return m_server.serverPort();
}

void FrameServer::handleNewConnections()
{
    // 循环处理所有挂起的新连接
    while (m_server.hasPendingConnections()) {
        QTcpSocket *socket = m_server.nextPendingConnection();
        if (!socket)
            break;

        // 禁用 Nagle 算法，降低发送延迟
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

        // 为新客户端分配状态对象并存入哈希表
        auto *state = new ClientState;
        m_clients.insert(socket, state);

        // 为该客户端创建一个独立的定时器，用于定时推送数据
        auto *timer = new QTimer(socket);
        state->timer = timer;
        timer->setInterval(m_intervalMs);

        // 定时器超时：发送一批数据帧
        connect(timer, &QTimer::timeout, this, [this, socket] {
            ClientState *current = m_clients.value(socket);
            if (current)
                sendBatch(socket, current);
        });
        // 收到客户端数据：解析数据帧
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            ClientState *current = m_clients.value(socket);
            if (current)
                handleClientData(socket, current);
        });
        // 客户端断开连接：清理状态
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            removeClient(socket);
        });

        // 打印新客户端连接日志
        emit logMessage(QStringLiteral("client connected: %1:%2")
                            .arg(socket->peerAddress().toString())
                            .arg(socket->peerPort()));

        timer->start();           // 启动定时器
        sendBatch(socket, state); // 立即发送第一帧
    }
}

void FrameServer::sendBatch(QTcpSocket *socket, ClientState *state)
{
    // 如果套接字不是连接状态，直接返回
    if (socket->state() != QAbstractSocket::ConnectedState)
        return;

    // 检查发送缓冲区。如果积压过多（慢客户端），跳过本次发送，避免内存无限增长
    if (socket->bytesToWrite() > kMaxQueuedBytes) {
        emit logMessage(QStringLiteral("client is slow, skipping a batch (queued %1 bytes)")
                            .arg(socket->bytesToWrite()));
        return;
    }

    // 构造一批模拟的遥测数据
    QList<TelemetrySample> samples;
    samples.reserve(m_samplesPerFrame);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < m_samplesPerFrame; ++i) {
        const quint64 sampleSeq = ++state->sampleSeq;
        TelemetrySample sample;
        sample.seq = sampleSeq;
        sample.timestampMs = now;
        sample.channel = static_cast<quint16>(1 + (i % 4));
        // 用正弦波生成模拟数据
        sample.value = 50.0 + 12.0 * std::sin(static_cast<double>(sampleSeq) * 0.05
                                              + static_cast<double>(sample.channel) * 0.8);
        // 随机制造一些 Bad 和 Warning 质量的数据
        if (sampleSeq % 997 == 0)
            sample.quality = Quality::Bad;
        else if (sampleSeq % 113 == 0)
            sample.quality = Quality::Warning;
        samples.append(sample);
    }

    // 将采样数据编码为二进制数据帧
    const EncodeResult encoded = proto::encodeTelemetryBatch(
        state->frameSeq++, static_cast<quint64>(now), QStringLiteral("lab06-server"), samples);
    if (!encoded.ok()) {
        emit logMessage(QStringLiteral("encode failed: %1").arg(encoded.detail));
        return;
    }

    // 发送数据，如果发送失败则断开连接
    const qint64 accepted = socket->write(encoded.frame);
    if (accepted < 0) {
        emit logMessage(QStringLiteral("write failed: %1").arg(socket->errorString()));
        socket->abort();
        return;
    }

    // 更新发送计数
    ++state->framesSent;
    ++m_framesSent;
}

void FrameServer::handleClientData(QTcpSocket *socket, ClientState *state)
{
    // 将收到的数据放入该客户端的解码器中
    state->decoder.append(socket->readAll());

    // 循环尝试解包
    for (;;) {
        const StreamResult result = state->decoder.nextFrame();
        // 数据不够，等待更多数据
        if (result.status == StreamStatus::NeedMoreData)
            break;

        // 协议错误（如 CRC 校验失败、长度越界等），断开连接
        if (result.status == StreamStatus::ProtocolError) {
            emit logMessage(QStringLiteral("client protocol error, closing: %1")
                                .arg(result.detail));
            socket->disconnectFromHost();
            return;
        }

        // 成功解出一个帧
        const auto &header = result.frame.header;
        if (header.type == MessageType::Heartbeat)
        {
            // 如果是心跳包，原样回发一个 Ack 帧（请求/响应模式）
            const EncodeResult ack = proto::encodeFrame(
                MessageType::Ack, header.sequence,
                static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()), {});

            if (ack.ok())
                socket->write(ack.frame);
            emit logMessage(QStringLiteral("heartbeat seq=%1 -> ack").arg(header.sequence));
        }
        else
        {
            // 其他类型的帧，仅打印日志
            emit logMessage(QStringLiteral("client frame type=%1 seq=%2")
                                .arg(static_cast<int>(header.type))
                                .arg(header.sequence));
        }
    }
}

void FrameServer::removeClient(QTcpSocket *socket)
{
    // 从哈希表中取出并移除对应的状态对象
    ClientState *state = m_clients.take(socket);
    if (!state)
        return;

    // 停止并销毁该客户端的定时器
    if (state->timer) {
        state->timer->stop();
        state->timer->deleteLater();
    }
    delete state; // 销毁状态对象

    // 打印剩余客户端数量
    emit logMessage(QStringLiteral("client disconnected, %1 remaining").arg(m_clients.size()));
    // 延迟销毁 socket 对象（安全地从事件循环中移除）
    socket->deleteLater();
}