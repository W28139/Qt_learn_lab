#include "logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <algorithm>
#include <cstdio>

Q_LOGGING_CATEGORY(lcApp, "app")
Q_LOGGING_CATEGORY(lcDb, "db")
Q_LOGGING_CATEGORY(lcCsv, "csv")

namespace logging {

namespace {

struct State
{
    QMutex mutex;
    bool installed = false;
    QFile file;
    QString dir;
    QString activePath;
    qint64 maxBytes = 1024 * 1024;
    int maxFiles = 5;
    qint64 bytesWritten = 0;
    QDate openDate;
    Level minimum = Level::Info;
};

State g_state;
QtMessageHandler g_previousHandler = nullptr;

Level severityOf(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return Level::Debug;
    case QtInfoMsg:
        return Level::Info;
    case QtWarningMsg:
        return Level::Warning;
    case QtCriticalMsg:
    case QtFatalMsg:
        return Level::Critical;
    }
    return Level::Info;
}

char levelTag(Level level)
{
    switch (level) {
    case Level::Debug:
        return 'D';
    case Level::Info:
        return 'I';
    case Level::Warning:
        return 'W';
    case Level::Critical:
        return 'C';
    }
    return '?';
}

QString threadName()
{
    const QString name = QThread::currentThread()->objectName();
    return name.isEmpty() ? QStringLiteral("gui") : name;
}

// 只格式化，不碰全局状态。日志文件写入时再加换行。
QString formatLine(Level level, const QMessageLogContext &context, const QString &message)
{
    const QString category = context.category
                                 ? QString::fromLatin1(context.category)
                                 : QStringLiteral("default");
    return QStringLiteral("%1 [%2] [%3/%4] %5")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
             QString(QChar::fromLatin1(levelTag(level))),
             category,
             threadName(),
             message);
}

// 以下 rotateLocked()/pruneLocked() 都要求 mutex 已持有。
void pruneLocked()
{
    QDir dir(g_state.dir);
    QFileInfoList rotated = dir.entryInfoList({QStringLiteral("app-*.log")},
                                              QDir::Files, QDir::NoSort);
    std::sort(rotated.begin(), rotated.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() < b.lastModified();
    });

    while (rotated.size() > g_state.maxFiles) {
        const QFileInfo oldest = rotated.takeFirst();
        if (!QFile::remove(oldest.absoluteFilePath())) {
            fprintf(stderr, "logging: cannot remove old log %s\n",
                    qPrintable(oldest.absoluteFilePath()));
        }
    }
}

void rotateLocked()
{
    if (g_state.file.isOpen())
        g_state.file.close();

    if (QFileInfo::exists(g_state.activePath)) {
        const QString stamp =
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
        QString target = QDir(g_state.dir).filePath(QStringLiteral("app-%1.log").arg(stamp));
        int suffix = 1;
        while (QFileInfo::exists(target)) {
            target = QDir(g_state.dir)
                         .filePath(QStringLiteral("app-%1-%2.log").arg(stamp).arg(suffix++));
        }

        // 轮转失败不影响继续写；下一次会再试。
        if (!QFile::rename(g_state.activePath, target))
            fprintf(stderr, "logging: cannot rotate %s\n", qPrintable(g_state.activePath));
    }

    pruneLocked();

    g_state.file.setFileName(g_state.activePath);
    if (!g_state.file.open(QIODevice::WriteOnly | QIODevice::Append))
        fprintf(stderr, "logging: cannot reopen %s: %s\n",
                qPrintable(g_state.activePath), qPrintable(g_state.file.errorString()));
    g_state.bytesWritten = g_state.file.size();
    g_state.openDate = QDate::currentDate();
}

void writeFileLocked(const QString &line)
{
    if (!g_state.file.isOpen())
        return;

    const QDate today = QDate::currentDate();
    const QByteArray data = line.toUtf8() + '\n';

    // 按大小或按天轮转：先转到历史文件，再写新文件。
    if (g_state.openDate.isValid()
        && (today != g_state.openDate
            || (g_state.bytesWritten > 0
                && g_state.bytesWritten + data.size() > g_state.maxBytes))) {
        rotateLocked();
    }

    if (!g_state.file.isOpen())
        return;

    if (g_state.file.write(data) == data.size()) {
        g_state.bytesWritten += data.size();
        // 每条都 flush：课程里优先保证崩溃时日志可见；
        // 高频日志对性能敏感时，可以改成批量 flush。
        if (!g_state.file.flush())
            fprintf(stderr, "logging: flush failed: %s\n",
                    qPrintable(g_state.file.errorString()));
    } else {
        fprintf(stderr, "logging: write failed: %s\n",
                qPrintable(g_state.file.errorString()));
    }
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)

{
    const Level level = severityOf(type);

    {
        QMutexLocker locker(&g_state.mutex);
        if (level < g_state.minimum)
            return;
    }

    const QString line = formatLine(level, context, message);

    // 控制台与文件分开加锁：fprintf 可能很慢，但这里不持锁。
    fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
    fflush(stderr);

    QMutexLocker locker(&g_state.mutex);
    if (g_state.installed)
        writeFileLocked(line);
}

void applyFilterRules(Level minimum)
{
    // Debug 时只打开我们自己的分类；Qt 内部的 qt.* debug 太多，
    // 会把日志文件迅速刷满，反而看不到应用日志。
    QLoggingCategory::setFilterRules(
        minimum == Level::Debug
            ? QStringLiteral("*.debug=true\nqt.*.debug=false")
            : QStringLiteral("*.debug=false"));
}

} // namespace

Level levelFromString(const QString &text, bool *ok)
{
    const QString lower = text.trimmed().toLower();
    if (ok)
        *ok = true;

    if (lower == QStringLiteral("debug"))
        return Level::Debug;
    if (lower == QStringLiteral("info"))
        return Level::Info;
    if (lower == QStringLiteral("warning") || lower == QStringLiteral("warn"))
        return Level::Warning;
    if (lower == QStringLiteral("critical") || lower == QStringLiteral("error"))
        return Level::Critical;

    if (ok)
        *ok = false;
    return Level::Info;
}

QString levelToString(Level level)
{
    switch (level) {
    case Level::Debug:
        return QStringLiteral("debug");
    case Level::Info:
        return QStringLiteral("info");
    case Level::Warning:
        return QStringLiteral("warning");
    case Level::Critical:
        return QStringLiteral("critical");
    }
    return QStringLiteral("info");
}

bool install(const QString &logDir, Level minimum, qint64 maxBytes, int maxFiles,
             QString *error)
{
    QMutexLocker locker(&g_state.mutex);

    if (g_state.installed)
        return true;

    if (logDir.isEmpty() || !QDir().mkpath(logDir)) {
        if (error)
            *error = QStringLiteral("cannot create log directory: %1").arg(logDir);
        return false;
    }

    g_state.dir = logDir;
    g_state.activePath = QDir(logDir).filePath(QStringLiteral("app.log"));
    g_state.maxBytes = qMax<qint64>(4096, maxBytes);
    g_state.maxFiles = qMax(1, maxFiles);
    g_state.minimum = minimum;
    g_state.openDate = QDate::currentDate();

    g_state.file.setFileName(g_state.activePath);
    if (!g_state.file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        if (error)
            *error = QStringLiteral("cannot open log file %1: %2")
                         .arg(g_state.activePath, g_state.file.errorString());
        return false;
    }
    g_state.bytesWritten = g_state.file.size();
    g_state.installed = true;

    // 先记录旧处理器，便于 shutdown() 恢复。
    g_previousHandler = qInstallMessageHandler(messageHandler);
    applyFilterRules(minimum);
    return true;
}

void setMinimumLevel(Level minimum)
{
    {
        QMutexLocker locker(&g_state.mutex);
        g_state.minimum = minimum;
    }
    applyFilterRules(minimum);
}

Level minimumLevel()
{
    QMutexLocker locker(&g_state.mutex);
    return g_state.minimum;
}

QString currentLogFile()
{
    QMutexLocker locker(&g_state.mutex);
    return g_state.activePath;
}

void shutdown()
{
    QMutexLocker locker(&g_state.mutex);
    if (!g_state.installed)
        return;

    // 先恢复默认处理器，之后关闭文件就安全了。
    qInstallMessageHandler(g_previousHandler);
    g_previousHandler = nullptr;

    if (g_state.file.isOpen()) {
        g_state.file.flush();
        g_state.file.close();
    }
    g_state.installed = false;
}

} // namespace logging