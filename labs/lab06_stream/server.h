#pragma once

#include "streamdecoder.h"

#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

class FrameServer final : public QObject
{
    Q_OBJECT

public:
    explicit FrameServer(QObject *parent = nullptr);
    ~FrameServer() override;

    // 启动监听。port 传 0 表示由系统分配；error 用于返回失败原因。
    bool listen(quint16 port, QString *error = nullptr);

    // 停止服务器：断开所有客户端并关闭监听。
    void stopAll();

    // 获取实际监听的端口（配合 listen(0) 使用）。
    quint16 serverPort() const;
    // 当前连接的客户端数量。
    int clientCount() const { return m_clients.size(); }
    // 服务器启动以来总共发出的帧数。
    quint64 framesSent() const { return m_framesSent; }

    // 设置向客户端发送数据的间隔（毫秒）。
    void setIntervalMs(int ms) { m_intervalMs = ms; }
    // 设置每个数据帧包含的采样点数量。
    void setSamplesPerFrame(int count) { m_samplesPerFrame = count; }

signals:
    // 用于向外（如终端 UI）发送日志文本。
    void logMessage(const QString &text);

private:
    // 每个客户端连接独立维护的状态。
    struct ClientState
    {
        QTimer *timer = nullptr;    // 该客户端的发送定时器
        StreamDecoder decoder;      // 解析该客户端发来数据的解码器
        quint32 frameSeq = 0;       // 发给该客户端的帧序号
        quint64 sampleSeq = 0;      // 发给该客户端的采样点序号
        quint64 framesSent = 0;     // 给该客户端已发送的帧数
    };

    // 处理新连接（由 QTcpServer::newConnection 触发）。
    void handleNewConnections();
    // 向指定客户端发送一批数据帧。
    void sendBatch(QTcpSocket *socket, ClientState *state);
    // 处理收到的客户端数据（解析帧）。
    void handleClientData(QTcpSocket *socket, ClientState *state);
    // 清理并移除断开连接的客户端。
    void removeClient(QTcpSocket *socket);

    QTcpServer m_server;                            // 底层 TCP 监听对象
    QHash<QTcpSocket *, ClientState *> m_clients;   // 管理所有客户端及其状态
    int m_intervalMs = 50;                          // 默认发送间隔 50ms
    int m_samplesPerFrame = 2;                      // 默认每帧 2 个采样
    quint64 m_framesSent = 0;                       // 全局已发总帧数统计
};