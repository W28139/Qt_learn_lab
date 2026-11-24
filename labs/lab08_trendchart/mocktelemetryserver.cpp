#include "mocktelemetryserver.h"

#include "protocol.h"

#include <QDateTime>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cmath>

namespace {

constexpr int kFrameIntervalMs = 50;     // 每个连接每 50 ms 发一帧
constexpr int kSamplesPerFrame = 4;      // 每帧 4 个样本，通道 1..4 各一个
constexpr qint64 kSlowClientLimit = 256 * 1024;  // 发送缓冲阈值（256 KiB）

} // namespace

MockTelemetryServer::MockTelemetryServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this)) // 随 server 对象一起析构
{
    // QTcpServer 只负责监听；每个接入的客户端单独建 socket、定时器和发送序号。
    connect(m_server, &QTcpServer::newConnection, this, &MockTelemetryServer::onNewConnection);
}

MockTelemetryServer::~MockTelemetryServer()
{
    stopAll();
}

bool MockTelemetryServer::listen(quint16 port, QString *error)
{
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        if (error)
            *error = m_server->errorString();
        return false;
    }

    emit logMessage(QStringLiteral("mock server listening on 127.0.0.1:%1")
                        .arg(m_server->serverPort()));
    return true;
}

void MockTelemetryServer::stopAll()
{
    // 先断开 socket 与本对象的信号连接，再 abort/deleteLater；
    // 否则 disconnected 回调可能在清理过程中重入 removeClient，访问已清空的表。
    const QList<Client *> clients = m_clients.values();
    m_clients.clear();

    for (Client *client : clients) {
        if (client->timer) {
            client->timer->stop();
            client->timer->deleteLater();
        }
        if (client->socket) {
            client->socket->disconnect(this);
            client->socket->abort();
            client->socket->deleteLater();
        }
        delete client;
    }
}

quint16 MockTelemetryServer::serverPort() const
{
    return m_server->serverPort();
}

int MockTelemetryServer::clientCount() const
{
    return m_clients.size();
}

void MockTelemetryServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket)
            break;

        auto *client = new Client;
        client->socket = socket;

        // 每个客户端一个定时器，各自按 20 Hz 发帧；
        // lambda 捕获 client 裸指针，client 的生命周期由 m_clients 和
        // removeClient()/stopAll() 管理，所以定时器必须在 client 释放前停掉。
        client->timer = new QTimer(this);
        client->timer->setInterval(kFrameIntervalMs);
        connect(client->timer, &QTimer::timeout, this, [this, client] {
            sendFrame(client);
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            removeClient(socket);
        });

        m_clients.insert(socket, client);
        client->timer->start();

        emit logMessage(QStringLiteral("client connected: %1:%2")
                            .arg(socket->peerAddress().toString())
                            .arg(socket->peerPort()));
    }
}

void MockTelemetryServer::sendFrame(Client *client)
{
    if (!client || !client->socket || !m_clients.contains(client->socket))
        return;
    if (client->socket->state() != QAbstractSocket::ConnectedState)
        return;

    // 慢客户端保护：发送缓冲超过上限就跳过本周期，不无限堆积。
    if (client->socket->bytesToWrite() > kSlowClientLimit) {
        emit logMessage(QStringLiteral("slow client, skipping a batch"));
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QList<TelemetrySample> samples;
    samples.reserve(kSamplesPerFrame);
    // 演示数据：4 个通道的正弦波，各自有不同的基线和相位，方便在趋势图上
    // 区分曲线；每 113 个样本标一次 Warning（本单元不再展示质量，只保留字段）。
    for (int i = 0; i < kSamplesPerFrame; ++i) {
        const quint32 index = client->sequence + static_cast<quint32>(i);
        const double channel = static_cast<double>(i + 1);
        TelemetrySample sample;
        sample.seq = static_cast<quint64>(index);
        sample.timestampMs = nowMs;
        sample.channel = static_cast<quint16>(i + 1);
        sample.value = 50.0 + (channel - 2.5) * 8.0
                       + 6.0 * std::sin(static_cast<double>(index) * 0.05 + channel)
                       + static_cast<double>(index % 7) * 0.1;
        sample.quality = (index % 113 == 0) ? Quality::Warning : Quality::Good;
        samples.append(sample);
    }

    const EncodeResult encoded =
        proto::encodeTelemetryBatch(client->sequence, static_cast<quint64>(nowMs),
                                    QStringLiteral("lab08-server"), samples);
    if (!encoded.ok()) {
        emit logMessage(QStringLiteral("encode failed: %1").arg(encoded.detail));
        return;
    }

    QByteArray frame = encoded.frame;
    ++client->sent;
    // 测试用：把最后一字节翻转，破坏 CRC，验证 worker 的协议错误处理。
    if (m_corruptAfterFrames > 0 && client->sent == m_corruptAfterFrames) {
        frame[frame.size() - 1] = static_cast<char>(frame.at(frame.size() - 1) ^ 0x01);
        emit logMessage(QStringLiteral("injected corrupted frame (seq=%1)")
                            .arg(client->sequence));
    }

    client->socket->write(frame);
    ++client->sequence;
}

void MockTelemetryServer::removeClient(QTcpSocket *socket)
{
    // 本函数由 socket 的 disconnected 信号触发（或 stopAll 之后），
    // 不能在这里直接 delete socket，用 deleteLater 等信号返回后再释放。
    Client *client = m_clients.take(socket);
    if (!client) {
        // 可能已经被 stopAll() 处理过。
        socket->deleteLater();
        return;
    }

    if (client->timer) {
        client->timer->stop();
        client->timer->deleteLater();
    }
    socket->deleteLater();
    delete client;

    emit logMessage(QStringLiteral("client disconnected, %1 remaining")
                        .arg(m_clients.size()));
}