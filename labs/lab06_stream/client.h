#pragma once

#include "streamdecoder.h"

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

// TCP 帧客户端：连接服务器、接收字节流、交给 StreamDecoder 解析成帧，
// 达到目标帧数后结束；支持断线重连与错误处理。
class FrameClient final : public QObject
{
    Q_OBJECT

public:
    // host/port: 服务器地址；targetFrames: 期望接收的帧数
    FrameClient(QString host, quint16 port, int targetFrames, QObject *parent = nullptr);

    void start();   // 开始连接
    void stop();    // 用户主动停止（不再重连）

    int receivedFrames() const { return m_receivedFrames; }        // 已接收帧数
    int receivedSamples() const { return m_receivedSamples; }      // 已接收样本数
    bool fatalError() const { return m_fatal; }                    // 是否发生不可恢复错误
    QString lastError() const { return m_lastError; }              // 最近一次错误描述
    quint64 discardedBytes() const { return m_decoder.discardedBytes(); } // 解码时丢弃的字节数

signals:
    void finished();                        // 任务结束（无论成功/失败）
    void logMessage(const QString &text);   // 日志输出

private:
    void onConnected();                     // 连接成功回调
    void onReadyRead();                     // 有数据可读回调
    void onDisconnected();                  // 连接断开回调
    void onSocketError(QAbstractSocket::SocketError error); // socket 错误回调
    void connectAgain();                    // 执行一次连接尝试

    void scheduleReconnect(const QString &reason); // 安排重连
    void fail(const QString &reason);              // 致命错误，终止任务
    void emitFinishedOnce();                       // 保证 finished 只发一次

    QTcpSocket m_socket;       // TCP 套接字
    StreamDecoder m_decoder;   // 字节流 → 帧 解码器
    QTimer m_reconnectTimer;   // 重连延时定时器
    QTimer m_connectTimer;     // 连接超时定时器
    QString m_host;            // 服务器主机
    quint16 m_port = 0;        // 服务器端口
    int m_targetFrames = 0;    // 目标帧数
    int m_receivedFrames = 0;  // 已接收帧数
    int m_receivedSamples = 0; // 已接收样本数
    int m_attempt = 0;         // 当前重连尝试次数
    bool m_reconnectScheduled = false; // 是否已安排重连（防重入）
    bool m_userStopped = false;        // 用户是否已主动 stop
    bool m_fatal = false;              // 是否不可恢复错误
    bool m_done = false;               // 任务是否已完成
    bool m_finishedEmitted = false;    // finished 信号是否已发出
    QString m_lastError;               // 最近错误描述
};