#include "databaseschema.h"
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
    QCoreApplication::setApplicationName(QStringLiteral("lab09_storage"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Lab 09: persist telemetry into SQLite and query it with pagination."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption hostOpt(
        QStringLiteral("host"),
        QStringLiteral("Connect to an external server instead of the built-in mock server."),
        QStringLiteral("host"));
    const QCommandLineOption portOpt(QStringLiteral("port"),
                                     QStringLiteral("Port of the external server."),
                                     QStringLiteral("port"), QStringLiteral("9000"));
    const QCommandLineOption dbOpt(
        QStringLiteral("db"),
        QStringLiteral("SQLite database file (default: ./lab09_telemetry.sqlite)."),
        QStringLiteral("path"), db::defaultDatabasePath());
    const QCommandLineOption autoOpt(
        QStringLiteral("auto-seconds"),
        QStringLiteral("Close the window automatically after N seconds (0 = never)."),
        QStringLiteral("seconds"), QStringLiteral("0"));
    const QCommandLineOption screenshotOpt(
        QStringLiteral("screenshot"),
        QStringLiteral("Save a PNG screenshot before closing (implies 5 seconds if "
                       "--auto-seconds is not set)."),
        QStringLiteral("path"));
    const QCommandLineOption historyOpt(
        QStringLiteral("history"),
        QStringLiteral("Open the history tab shortly after startup."));
    const QCommandLineOption stopOpt(
        QStringLiteral("stop-after-seconds"),
        QStringLiteral("Stop capturing after N seconds without closing the window "
                       "(0 = never). Useful for verifying session finalization."),
        QStringLiteral("seconds"), QStringLiteral("0"));

    parser.addOptions({hostOpt, portOpt, dbOpt, autoOpt, screenshotOpt, historyOpt, stopOpt});
    parser.process(app);

    bool autoOk = false;
    int autoSeconds = parser.value(autoOpt).toInt(&autoOk);
    if (!autoOk || autoSeconds < 0) {
        qCritical().noquote() << "invalid --auto-seconds:" << parser.value(autoOpt);
        return 2;
    }

    bool stopOk = false;
    int stopSeconds = parser.value(stopOpt).toInt(&stopOk);
    if (!stopOk || stopSeconds < 0) {
        qCritical().noquote() << "invalid --stop-after-seconds:" << parser.value(stopOpt);
        return 2;
    }

    const QString screenshotPath = parser.value(screenshotOpt);
    if (!screenshotPath.isEmpty() && autoSeconds == 0)
        autoSeconds = 5;

    const QString dbPath = parser.value(dbOpt);

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

    MainWindow window(dbPath, host, port, embedded);
    window.show();

    if (parser.isSet(historyOpt)) {
        // 先让采集跑一会儿，历史页才有可看的会话和分页。
        QTimer::singleShot(1500, &window, &MainWindow::showHistoryTab);
    }

    if (stopSeconds > 0) {
        // 模拟用户点“停止”：停 worker、排空队列、结束会话，但窗口继续运行。
        QTimer::singleShot(stopSeconds * 1000, &window, [&window] {
            qInfo().noquote() << "[gui] auto stop requested";
            window.stopCapture();
        });
    }

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