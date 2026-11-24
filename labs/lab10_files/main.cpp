#include "apppaths.h"
#include "appsettings.h"
#include "logging.h"
#include "mainwindow.h"
#include "mocktelemetryserver.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QPixmap>
#include <QTimer>

#include <cstdio>
#include <memory>

namespace {

// 把命令行传入的字符串解析成“非负整数”。
// 失败（非数字 / 负数）返回 false，由调用方决定报错并退出。
bool parseNonNegative(const QString &text, int *value)
{
    bool ok = false;
    const int parsed = text.toInt(&ok);
    if (!ok || parsed < 0)
        return false;
    *value = parsed;
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // QStandardPaths/QSettings 的组织名、应用名必须在读取路径之前设置，
    // 否则标准路径和配置文件位置会算错。
    QCoreApplication::setOrganizationName(QStringLiteral("QtLearn"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("qtlearn.local"));
    QCoreApplication::setApplicationName(QStringLiteral("lab10_files"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    // ===== 命令行参数定义 =====
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Lab 10: standard paths, QSettings, CSV export and rotating logs."));
    parser.addHelpOption();    // 自动 -h/--help
    parser.addVersionOption(); // 自动 -v/--version

    // --host：指定外部服务器地址；不指定则使用内置 mock 服务器
    const QCommandLineOption hostOpt(
        QStringLiteral("host"),
        QStringLiteral("Connect to an external server instead of the built-in mock server."),
        QStringLiteral("host"));
    // --port：外部服务器端口（默认 9000），仅配合 --host 使用
    const QCommandLineOption portOpt(QStringLiteral("port"),
                                     QStringLiteral("Port of the external server."),
                                     QStringLiteral("port"), QStringLiteral("9000"));
    // --data-dir：把所有数据/配置/日志/导出都放到指定目录（便于无头验证）
    const QCommandLineOption dataDirOpt(
        QStringLiteral("data-dir"),
        QStringLiteral("Put data, config, logs and exports under this directory "
                       "(experiments/headless verification)."),
        QStringLiteral("path"));
    // --db：显式指定 SQLite 数据库文件，优先级高于设置文件
    const QCommandLineOption dbOpt(
        QStringLiteral("db"),
        QStringLiteral("SQLite database file (default: from settings or dataDir)."),
        QStringLiteral("path"));
    // --log-level：日志级别，默认从设置文件读取
    const QCommandLineOption logLevelOpt(
        QStringLiteral("log-level"),
        QStringLiteral("debug|info|warning|critical (default: from settings)."),
        QStringLiteral("level"));
    // --log-max-kb：单个日志文件达到多少 KiB 后轮转，默认 1024
    const QCommandLineOption logMaxOpt(
        QStringLiteral("log-max-kb"),
        QStringLiteral("Rotate the active log file after N KiB (default 1024)."),
        QStringLiteral("kb"), QStringLiteral("1024"));
    // --log-files：保留多少个已轮转的日志文件，默认 5
    const QCommandLineOption logFilesOpt(
        QStringLiteral("log-files"),
        QStringLiteral("Keep at most N rotated log files (default 5)."),
        QStringLiteral("count"), QStringLiteral("5"));
    // --auto-seconds：N 秒后自动关闭窗口（0 = 不自动关闭）
    const QCommandLineOption autoOpt(
        QStringLiteral("auto-seconds"),
        QStringLiteral("Close the window automatically after N seconds (0 = never)."),
        QStringLiteral("seconds"), QStringLiteral("0"));
    // --screenshot：关闭前保存 PNG 截图（若未设 --auto-seconds 则隐式设为 5 秒）
    const QCommandLineOption screenshotOpt(
        QStringLiteral("screenshot"),
        QStringLiteral("Save a PNG screenshot before closing (implies 5 seconds if "
                       "--auto-seconds is not set)."),
        QStringLiteral("path"));
    // --history：启动后自动切到历史标签页（先让采集跑一会儿）
    const QCommandLineOption historyOpt(
        QStringLiteral("history"),
        QStringLiteral("Open the history tab shortly after startup."));
    // --stop-after-seconds：N 秒后模拟“停止采集”，但不关窗口（用于验证会话收尾）
    const QCommandLineOption stopOpt(
        QStringLiteral("stop-after-seconds"),
        QStringLiteral("Stop capturing after N seconds without closing the window "
                       "(0 = never). Useful for verifying session finalization."),
        QStringLiteral("seconds"), QStringLiteral("0"));
    // --export-after-seconds：N 秒后把当前/选中会话导出为 CSV（无头验证用）
    const QCommandLineOption exportOpt(
        QStringLiteral("export-after-seconds"),
        QStringLiteral("Export the selected/current session to CSV after N seconds "
                       "(0 = never). Useful for headless verification."),
        QStringLiteral("seconds"), QStringLiteral("0"));

    // 注册所有选项并解析命令行；出错（如未知选项）时 parser 内部会退出
    parser.addOptions({hostOpt, portOpt, dataDirOpt, dbOpt, logLevelOpt, logMaxOpt,
                       logFilesOpt, autoOpt, screenshotOpt, historyOpt, stopOpt, exportOpt});
    parser.process(app);

    // ===== 解析并校验数值型参数 =====
    int autoSeconds = 0;
    int stopSeconds = 0;
    int exportSeconds = 0;
    if (!parseNonNegative(parser.value(autoOpt), &autoSeconds)) {
        qCritical().noquote() << "invalid --auto-seconds:" << parser.value(autoOpt);
        return 2;
    }
    if (!parseNonNegative(parser.value(stopOpt), &stopSeconds)) {
        qCritical().noquote() << "invalid --stop-after-seconds:" << parser.value(stopOpt);
        return 2;
    }
    if (!parseNonNegative(parser.value(exportOpt), &exportSeconds)) {
        qCritical().noquote() << "invalid --export-after-seconds:" << parser.value(exportOpt);
        return 2;
    }

    // 日志大小/数量必须为正数
    int logMaxKb = 0;
    int logFiles = 0;
    if (!parseNonNegative(parser.value(logMaxOpt), &logMaxKb) || logMaxKb <= 0) {
        qCritical().noquote() << "invalid --log-max-kb:" << parser.value(logMaxOpt);
        return 2;
    }
    if (!parseNonNegative(parser.value(logFilesOpt), &logFiles) || logFiles <= 0) {
        qCritical().noquote() << "invalid --log-files:" << parser.value(logFilesOpt);
        return 2;
    }

    // 只给了截图路径却没给自动关闭时间：默认 5 秒后截图并关闭
    const QString screenshotPath = parser.value(screenshotOpt);
    if (!screenshotPath.isEmpty() && autoSeconds == 0)
        autoSeconds = 5;

    // 1) 路径：用 QStandardPaths 推导，或用 --data-dir 覆盖。
    //    所有子目录（data/config/logs/exports）在这里统一创建。
    QString pathsError;
    const apppaths::Paths paths = apppaths::resolve(parser.value(dataDirOpt), &pathsError);
    if (paths.dataDir.isEmpty()) {
        fprintf(stderr, "cannot prepare application paths: %s\n",
                qPrintable(pathsError));
        return 2;
    }

    // 2) 配置：使用显式 INI 文件（settingsFile），方便用户直接查看/编辑。
    AppSettings settings(paths.settingsFile);

    // 3) 日志：尽早安装，后续启动流程和线程日志才能写入文件。
    //    级别优先取命令行，其次取设置文件，最后默认 info。
    const QString levelText =
        parser.isSet(logLevelOpt) ? parser.value(logLevelOpt)
                                  : settings.logLevel(QStringLiteral("info"));
    bool levelOk = false;
    const logging::Level logLevel = logging::levelFromString(levelText, &levelOk);
    if (!levelOk) {
        fprintf(stderr, "invalid log level: %s\n", qPrintable(levelText));
        return 2;
    }

    QString logError;
    // 安装滚动文件日志：目录、级别、单文件上限（KiB 转字节）、保留数量
    if (!logging::install(paths.logDir, logLevel, static_cast<qint64>(logMaxKb) * 1024,
                          logFiles, &logError)) {
        fprintf(stderr, "cannot install file logging: %s\n", qPrintable(logError));
        // 注意：日志安装失败不致命，程序继续运行（后续日志只输出到控制台）
    }

    // 启动信息：把关键路径和当前配置打到日志里，便于排查
    qCInfo(lcApp).noquote() << "lab10_files starting";
    qCInfo(lcApp).noquote() << "[paths] data:" << paths.dataDir;
    qCInfo(lcApp).noquote() << "[paths] config:" << paths.configDir;
    qCInfo(lcApp).noquote() << "[paths] logs:" << paths.logDir;
    qCInfo(lcApp).noquote() << "[paths] default exports:" << paths.exportDir;
    qCInfo(lcApp).noquote() << "[settings] file:" << paths.settingsFile;
    qCInfo(lcApp).noquote() << "[settings] pageSize:" << settings.pageSize(100)
                            << "autoStart:" << settings.autoStart(true)
                            << "utf8Bom:" << settings.utf8Bom(true)
                            << "exportDir:" << settings.exportDir(paths.exportDir);
    qCInfo(lcApp).noquote() << "[logging] level:" << logging::levelToString(logLevel)
                            << "rotate at" << logMaxKb << "KiB, keep" << logFiles
                            << "files:" << logging::currentLogFile();

    // 4) 数据库路径优先级：命令行 --db > 设置文件 > 默认 dataDir 下的 db。
    const QString dbPath = parser.isSet(dbOpt) ? parser.value(dbOpt)
                                               : settings.dbPath(paths.dbPath);

    // ===== 服务器准备：外部服务器 or 内置 mock =====
    QString host = QStringLiteral("127.0.0.1");
    quint16 port = 0;
    bool embedded = true; // true 表示使用内置 mock 服务器
    std::unique_ptr<MockTelemetryServer> server;

    if (parser.isSet(hostOpt)) {
        // 用户显式指定了外部服务器：使用 --host + --port
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
        // 未指定外部服务器：启动内置 mock 服务器，监听随机空闲端口（0 = 系统分配）
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

    // 把最终生效的数据库路径回填到 paths，再传给主窗口
    apppaths::Paths effectivePaths = paths;
    effectivePaths.dbPath = dbPath;

    // ===== 创建并显示主窗口 =====
    MainWindow window(effectivePaths, host, port, embedded, settings);
    window.show();

    // --history：先让采集跑 1.5 秒，历史页才有可展示的会话，再切过去
    if (parser.isSet(historyOpt)) {
        QTimer::singleShot(1500, &window, &MainWindow::showHistoryTab);
    }

    // --stop-after-seconds：到点模拟“停止采集”（停 worker、排空队列、结束会话），
    // 但窗口继续运行，便于观察停止后的状态。
    if (stopSeconds > 0) {
        QTimer::singleShot(stopSeconds * 1000, &window, [&window] {
            qInfo().noquote() << "[gui] auto stop requested";
            window.stopCapture();
        });
    }

    // --export-after-seconds：到点自动导出当前/选中会话为 CSV
    if (exportSeconds > 0) {
        QTimer::singleShot(exportSeconds * 1000, &window, [&window] {
            qCInfo(lcCsv).noquote() << "auto export requested";
            window.exportSelectedSessionAuto();
        });
    }

    // --auto-seconds / --screenshot：到点（可选截图）后关闭窗口，退出事件循环
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

    // 进入 Qt 事件循环，直到窗口关闭
    const int exitCode = app.exec();
    // 退出前记录日志并关闭日志系统，确保缓冲区刷盘
    qCInfo(lcApp).noquote() << "lab10_files exiting";
    logging::shutdown();
    return exitCode;
}