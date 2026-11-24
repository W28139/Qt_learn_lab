#include "mainwindow.h"
#include "mocktelemetryserver.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

#include <memory>

int main(int argc, char *argv[])
{
    // 创建 Qt GUI 应用
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("lab07_worker"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    // 命令行参数解析器
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Lab 07: QThread worker model, thread-safe queue, graceful exit."));
    parser.addHelpOption();     // --help
    parser.addVersionOption();  // --version

    // --host：连外部服务器（不指定则用内嵌 mock server）
    const QCommandLineOption hostOpt(
        QStringLiteral("host"),
        QStringLiteral("Connect to an external server instead of the built-in mock server."),
        QStringLiteral("host"));
    // --port：外部服务器端口，默认 9000
    const QCommandLineOption portOpt(QStringLiteral("port"),
                                     QStringLiteral("Port of the external server."),
                                     QStringLiteral("port"), QStringLiteral("9000"));
    // --auto-seconds：N 秒后自动关窗，0 表示不自动关
    const QCommandLineOption autoOpt(
        QStringLiteral("auto-seconds"),
        QStringLiteral("Close the window automatically after N seconds (0 = never)."),
        QStringLiteral("seconds"), QStringLiteral("0"));

    parser.addOptions({hostOpt, portOpt, autoOpt});
    parser.process(app);  // 解析参数

    // 校验 --auto-seconds
    bool autoOk = false;
    int autoSeconds = parser.value(autoOpt).toInt(&autoOk);
    if (!autoOk || autoSeconds < 0) {
        qCritical().noquote() << "invalid --auto-seconds:" << parser.value(autoOpt);
        return 2;
    }

    // 默认连接参数：本机、端口稍后确定、嵌入式模式
    QString host = QStringLiteral("127.0.0.1");
    quint16 port = 0;
    bool embedded = true;
    std::unique_ptr<MockTelemetryServer> server;  // 仅嵌入式模式才创建

    if (parser.isSet(hostOpt)) {
        // 外部模式：连用户指定的 host:port
        host = parser.value(hostOpt);

        bool portOk = false;
        const uint parsed = parser.value(portOpt).toUInt(&portOk);
        if (!portOk || parsed == 0 || parsed > 65535) {
            qCritical().noquote() << "invalid --port:" << parser.value(portOpt);
            return 2;
        }
        port = static_cast<quint16>(parsed);
        embedded = false;
    }
    else {
        // 默认模式：主线程里起一个最小的 mock server，GUI 无需第二个进程。
        server = std::make_unique<MockTelemetryServer>();
        // 把 mock server 的日志转发到 qInfo
        QObject::connect(server.get(), &MockTelemetryServer::logMessage,
                         [](const QString &message) {
                             qInfo().noquote() << "[mock]" << message;
                         });

        // 监听随机空闲端口（0 = 系统自动分配）
        QString error;
        if (!server->listen(0, &error)) {
            qCritical().noquote() << "mock server failed to listen:" << error;
            return 1;
        }
        port = server->serverPort();  // 取实际分配的端口
    }

    // 创建并显示主窗口（内部会创建 worker、启动线程）
    MainWindow window(host, port, embedded);
    window.show();

    // 若指定自动关闭时间，则定时关闭窗口
    if (autoSeconds > 0) {
        QTimer::singleShot(autoSeconds * 1000, &window, &QWidget::close);
    }

    // 进入 Qt 事件循环
    return app.exec();
}