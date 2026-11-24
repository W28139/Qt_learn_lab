// 程序形态：无 GUI 的终端程序（Qt 控制台程序）。
//   - 通过 QCoreApplication 而非 QApplication，故不会创建任何窗口；
//   - 通过 QCommandLineParser 解析终端传入的命令行参数；
//   - 所有日志通过 qInfo()/qCritical() 输出到终端（Qt 默认走 stderr）。
//
// 两种互斥的运行模式（必须且只能选一个）：
//   --server     启动 TCP 分帧服务器，监听端口、接受连接、收/发数据帧；
//   --client     启动 TCP 分帧客户端，连接服务器并发送/接收数据帧。


#include "client.h"
#include "server.h"

#include <QCommandLineOption>   // 定义单个命令行选项（含短名/长名/说明/默认值）
#include <QCommandLineParser>   // 解析 argv，提供 isSet()/value() 等访问接口
#include <QCoreApplication>     // 无 GUI 的 Qt 应用对象，提供事件循环 exec()
#include <QDebug>               // qInfo()/qCritical() 等日志输出宏
#include <QTimer>               // 单次定时器，用于“运行时长限制”功能

namespace {


// 把 命令行 传入的端口字符串解析为 quint16
quint16 parsePort(const QString &text, bool *ok)
{
    bool good = false;
    const uint value = text.toUInt(&good);

    *ok = good && value <= 65535;

    return static_cast<quint16>(value);
}

// 把 命令行 传入的“计数类”字符串解析为非负 int
int parseCount(const QString &text, bool *ok)
{
    const int value = text.toInt(ok);

    if (*ok && value < 0) {
        *ok = false;
        return 0;
    }
    return value;
}

} // namespace



int main(int argc, char *argv[])
{
    // 创建无 GUI 的 Qt 应用对象。
    // 它管理事件循环、定时器、对象生命周期；后续所有 QObject 都依附于它所在的线程。
    QCoreApplication app(argc, argv);

    // 设置应用名与版本号，供 --help / --version 展示。
    QCoreApplication::setApplicationName(QStringLiteral("lab06_stream"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    // ---------- 1. 命令行解析器配置 ----------
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Lab 06: TCP stream framing, server and client."));

    // 注册 -h/--help 与 -v/--version 两个内置选项
    parser.addHelpOption();
    parser.addVersionOption();

    // ---- 模式选项（二者互斥，必须且只能出现一个）----

    // --server：以服务器模式运行
    const QCommandLineOption serverOpt(QStringLiteral("server"),
                                       QStringLiteral("Run the TCP frame server."));

    // --client：以客户端模式运行
    const QCommandLineOption clientOpt(QStringLiteral("client"),
                                       QStringLiteral("Run the TCP frame client."));

    // ---- 通用参数（服务器/客户端可能用到）----

    // --host：客户端连接的目标主机名或 IP。
    const QCommandLineOption hostOpt(QStringLiteral("host"),    //选项名称 Qt 会自动把它识别为 --host
                                     QStringLiteral("Client target host."), // 描述，显示在 --help 里
                                     QStringLiteral("host"), // 值名称。表示该选项需要一个值
                                     QStringLiteral("127.0.0.1"));  // 默认值

    // --port：端口号。默认 9000。
    // 服务器模式下传 0 表示由操作系统分配一个空闲端口；
    // 客户端模式下应传入服务器实际监听的端口。
    const QCommandLineOption portOpt(QStringLiteral("port"),
                                     QStringLiteral("Port (0 = OS-assigned for server)."),
                                     QStringLiteral("port"), QStringLiteral("9000"));

    // --frames：客户端在收到/发送 N 帧后主动结束；0 表示不限制（一直运行）。
    // 默认 100。
    const QCommandLineOption framesOpt(QStringLiteral("frames"),
                                       QStringLiteral("Client stops after N frames (0 = never)."),
                                       QStringLiteral("count"), QStringLiteral("100"));

    // --seconds：运行时长限制（秒）。
    // 默认 0，但语义分模式不同：
    //   - 服务器：永久运行，直到被外部终止；
    //   - 客户端：采用默认 30 秒超时。
    //
    const QCommandLineOption secondsOpt(QStringLiteral("seconds"),
                                        QStringLiteral("Run time limit (0 = server forever / "
                                                       "client 30 s timeout)."),
                                        QStringLiteral("seconds"), QStringLiteral("0"));

    // 一次性把全部选项注册到解析器
    parser.addOptions({serverOpt, clientOpt, hostOpt, portOpt, framesOpt, secondsOpt});

    // 解析 argv。若出现未知选项/缺少值，parser 会自行打印错误并退出（退出码非 0）。
    parser.process(app);

    // ---------- 2. 运行模式校验 ----------
    const bool asServer = parser.isSet(serverOpt);
    const bool asClient = parser.isSet(clientOpt);

    // 两种模式必须“恰好选择一个”：
    //   - 一个都没选 → 用户不知道要做什么，打印帮助并以 2 退出；
    //   - 选了多个     → 语义冲突，同样打印帮助并以 2 退出。
    if (asServer == asClient)
        parser.showHelp(2);

    // ---------- 3. 通用参数解析与校验 ----------
    bool ok = false;

    // 解析端口；失败时打印原始字符串便于用户定位，退出码 2。
    const quint16 port = parsePort(parser.value(portOpt), &ok);
    if (!ok) {
        qCritical().noquote() << "invalid port:" << parser.value(portOpt);
        return 2;
    }

    // 解析帧数量；负数或非数字都视为非法，退出码 2。
    const int frames = parseCount(parser.value(framesOpt), &ok);
    if (!ok) {
        qCritical().noquote() << "invalid frame count:" << parser.value(framesOpt);
        return 2;
    }

    // 解析运行秒数；同样要求非负整数，退出码 2。
    const int seconds = parseCount(parser.value(secondsOpt), &ok);
    if (!ok) {
        qCritical().noquote() << "invalid seconds:" << parser.value(secondsOpt);
        return 2;
    }

    // ---------- 4. 服务器模式 ----------
    if (asServer) {
        // 服务器对象在栈上创建，生命周期覆盖整个事件循环。
        // 它内部管理监听 socket 以及若干个客户端连接。
        FrameServer server;

        // 信号槽连接：把服务器内部产生的每条日志转发到终端。
        QObject::connect(&server, &FrameServer::logMessage, [](const QString &message) {
            qInfo().noquote() << "[server]" << message;
        });

        // 开始监听指定端口。
        QString error;
        if (!server.listen(port, &error)) {
            qCritical().noquote() << "listen failed:" << error;
            return 1;   // 运行期错误 → 退出码 1
        }

        // 打印实际监听端口
        qInfo().noquote() << QStringLiteral("server listening on port %1")
                                 .arg(server.serverPort());

        // 若用户指定了 --seconds > 0，则设置一个单次定时，到点后停止所有连接并退出事件循环。
        if (seconds > 0) {
            QTimer::singleShot(seconds * 1000, &app, [&server, &app] {
                qInfo().noquote() << "[server] time limit reached, stopping";
                server.stopAll();   // 主动关闭所有连接，保证对端能感知断开
                app.quit();         // 退出事件循环，使下面的 app.exec() 返回
            });
        }

        // 进入事件循环：处理新连接、可读事件、定时器等，直到 app.quit() 被调用。
        const int rc = app.exec();

        // 事件循环退出后再次确保所有连接被关闭（兜底清理）。
        server.stopAll();
        return rc;
    }


    // ---------- 5. 客户端模式 ----------
    // 构造客户端：目标主机、端口、以及“收到/发够 N 帧后结束”的上限。
    FrameClient client(parser.value(hostOpt), port, frames);

    // 信号槽连接：把客户端日志转发到终端
    QObject::connect(&client, &FrameClient::logMessage, [](const QString &message) {
        qInfo().noquote() << "[client]" << message;
    });

    // 信号槽连接：客户端正常完成（发够帧数或对端关闭）时发出 finished，
    // 直接连到 QCoreApplication::quit，从而结束事件循环。
    QObject::connect(&client, &FrameClient::finished, &app, &QCoreApplication::quit);

    // 启动客户端（异步发起连接），真正的收发在事件循环中推进。
    client.start();

    // 计算本次运行的时长上限
    const int limitSeconds = seconds > 0 ? seconds : 30;

    // 单次定时器：到点后停止客户端并退出事件循环。
    QTimer::singleShot(limitSeconds * 1000, &app, [&client, &app, limitSeconds] {
        qInfo().noquote() << QStringLiteral("[client] %1 s limit reached, stopping")
        .arg(limitSeconds);
        client.stop();   // 主动断开连接并停止收发
        app.quit();      // 退出事件循环
    });

    // 进入事件循环，直到：
    //   - 客户端自然完成（finished → quit）；
    //   - 或超时定时器触发（stop + quit）。
    const int rc = app.exec();

    // 最终退出码：
    //   - 若客户端期间发生过致命错误（如连接失败、协议错误），返回 1；
    //   - 否则沿用事件循环的退出码（通常为 0）。
    return client.fatalError() ? 1 : rc;
}