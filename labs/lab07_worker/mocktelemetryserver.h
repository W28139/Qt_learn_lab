#pragma once

#include <QHash>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;
class QTimer;

// 运行在主线程里的最小遥测服务端，用来给 lab07 的 worker 供数
// 每个连接 50 ms 发一帧（2 个样本）；可选在第 N 帧故意破坏 CRC，
// 用来验证 worker 对协议错误的处理。
class MockTelemetryServer final : public QObject
{
    Q_OBJECT

public:
    explicit MockTelemetryServer(QObject *parent = nullptr);
    ~MockTelemetryServer() override;

    // 在指定端口开始监听；port = 0 表示由系统分配空闲端口。
    bool listen(quint16 port, QString *error);
    // 主动关闭所有客户端连接与服务器监听。
    void stopAll();

    quint16 serverPort() const;
    int clientCount() const;     // 当前已连接的客户端数量

    // 0 = 不破坏；N > 0 表示该连接发出的第 N 帧 CRC 被翻转。
    void setCorruptAfterFrames(int frames) { m_corruptAfterFrames = frames; }

signals:
    void logMessage(const QString &text);

private:
    struct Client
    {
        QTcpSocket *socket = nullptr;  // 归属该连接的套接字
        QTimer *timer = nullptr;       // 该连接专属的发帧定时器（50 ms 周期）
        quint32 sequence = 0;          // 帧序号，自增，随帧发送
        int sent = 0;                  // 已发送帧数，用于与 m_corruptAfterFrames 比较
    };

    // QTcpServer 有新连接时的槽：为新 socket 创建 Client 和定时器
    void onNewConnection();
    // 组装并发送一帧
    void sendFrame(Client *client);
    // 移除并清理某个客户端
    void removeClient(QTcpSocket *socket);

    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, Client *> m_clients;
    int m_corruptAfterFrames = 0;   // 0 = 不注入故障；N>0 时第 N 帧破坏 CRC
};