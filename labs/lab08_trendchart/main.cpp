#include "mainwindow.h"
#include "mocktelemetryserver.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QPixmap>
#include <QTimer>

#include <memory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("lab08_trendchart"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Lab 08: QPainter realtime trend chart."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption hostOpt(
        QStringLiteral("host"),
        QStringLiteral("Connect to an external server instead of the built-in mock server."),
        QStringLiteral("host"));
    const QCommandLineOption portOpt(QStringLiteral("port"),
                                     QStringLiteral("Port of the external server."),
                                     QStringLiteral("port"), QStringLiteral("9000"));
    const QCommandLineOption autoOpt(
        QStringLiteral("auto-seconds"),
        QStringLiteral("Close the window automatically after N seconds (0 = never)."),
        QStringLiteral("seconds"), QStringLiteral("0"));
    const QCommandLineOption screenshotOpt(
        QStringLiteral("screenshot"),
        QStringLiteral("Save a PNG screenshot before closing (implies 3 seconds if "
                       "--auto-seconds is not set)."),
        QStringLiteral("path"));

    parser.addOptions({hostOpt, portOpt, autoOpt, screenshotOpt});
    parser.process(app);

    bool autoOk = false;
    int autoSeconds = parser.value(autoOpt).toInt(&autoOk);
    if (!autoOk || autoSeconds < 0) {
        qCritical().noquote() << "invalid --auto-seconds:" << parser.value(autoOpt);
        return 2;
    }

    const QString screenshotPath = parser.value(screenshotOpt);
    if (!screenshotPath.isEmpty() && autoSeconds == 0)
        autoSeconds = 3;

    QString host = QStringLiteral("127.0.0.1");
    quint16 port = 0;
    bool embedded = true;
    std::unique_ptr<MockTelemetryServer> server;

    if (parser.isSet(hostOpt)) {
        host = parser.value(hostOpt);

        bool portOk = false;
        const uint parsed = parser.value(portOpt).toUInt(&portOk);
        if (!portOk || parsed == 0 || parsed > 65535) {
            qCritical().noquote() << "invalid --port:" << parser.value(portOpt);
            return 2;
        }
        port = static_cast<quint16>(parsed);
        embedded = false;
    } else {
        server = std::make_unique<MockTelemetryServer>();
        QObject::connect(server.get(), &MockTelemetryServer::logMessage,
                         [](const QString &message) {
                             qInfo().noquote() << "[mock]" << message;
                         });

        QString error;
        if (!server->listen(0, &error)) {
            qCritical().noquote() << "mock server failed to listen:" << error;
            return 1;
        }
        port = server->serverPort();
    }

    MainWindow window(host, port, embedded);
    window.show();

    if (autoSeconds > 0) {
        QTimer::singleShot(autoSeconds * 1000, &window,
                           [&window, screenshotPath] {
                               if (!screenshotPath.isEmpty()) {
                                   if (window.grab().save(screenshotPath))
                                       qInfo().noquote()
                                           << "[gui] screenshot saved:" << screenshotPath;
                                   else
                                       qWarning().noquote()
                                           << "[gui] failed to save screenshot:" << screenshotPath;
                               }
                               window.close();
                           });
    }

    return app.exec();
}