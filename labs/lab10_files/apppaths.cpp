#include "apppaths.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace apppaths {

namespace {

QString standardDataDir()
{
    // 依赖 setOrganizationName()/setApplicationName() 已经设置好。
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir.isEmpty() ? QDir::homePath() + QStringLiteral("/.lab10_files") : dir;
}

QString standardConfigDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return dir.isEmpty() ? standardDataDir() : dir;
}

QString standardExportDir()
{
    // 导出文件是给用户看的，优先放“文档”目录；没有就退回数据目录。
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (docs.isEmpty())
        docs = standardDataDir();
    return QDir(docs).filePath(QStringLiteral("lab10_exports"));
}

} // namespace

Paths resolve(const QString &dataDirOverride, QString *error)
{
    Paths paths;

    if (!dataDirOverride.isEmpty()) {
        paths.dataDir = QDir(dataDirOverride).absolutePath();
        paths.configDir = QDir(paths.dataDir).filePath(QStringLiteral("config"));
        paths.logDir = QDir(paths.dataDir).filePath(QStringLiteral("logs"));
        paths.exportDir = QDir(paths.dataDir).filePath(QStringLiteral("exports"));
    } else {
        paths.dataDir = standardDataDir();
        paths.configDir = standardConfigDir();
        paths.logDir = QDir(paths.dataDir).filePath(QStringLiteral("logs"));
        paths.exportDir = standardExportDir();
    }

    paths.dbPath = QDir(paths.dataDir).filePath(QStringLiteral("telemetry.sqlite"));
    paths.settingsFile = QDir(paths.configDir).filePath(QStringLiteral("settings.ini"));
    paths.logFile = QDir(paths.logDir).filePath(QStringLiteral("app.log"));

    if (!ensureDirs(paths, error))
        return {};

    return paths;
}

bool ensureDirs(const Paths &paths, QString *error)
{
    const QStringList dirs = {
        paths.dataDir,
        paths.configDir,
        paths.logDir,
        paths.exportDir,
        QFileInfo(paths.dbPath).absolutePath(),
    };

    for (const QString &dir : dirs) {
        if (dir.isEmpty() || QDir().mkpath(dir))
            continue;
        if (error)
            *error = QStringLiteral("cannot create directory: %1").arg(dir);
        return false;
    }
    return true;
}

} // namespace apppaths