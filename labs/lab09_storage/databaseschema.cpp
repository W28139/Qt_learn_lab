#include "databaseschema.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

namespace db {

namespace {

// schema 版本。程序自己"支持到哪一版 schema"的编译期常量
constexpr int kSchemaVersion = 1;

// 执行单条 SQL
bool execOne(QSqlDatabase &db, const QString &sql, QString *error)
{
    QSqlQuery query(db);
    if (query.exec(sql))
        return true;
    if (error)
        *error = QStringLiteral("%1\n  SQL: %2").arg(describeError(query.lastError()), sql);
    return false;
}

// 顺序执行一组语句，遇错即停（建表/建索引/迁移都是有依赖顺序的）。
bool execAll(QSqlDatabase &db, const QStringList &statements, QString *error)
{
    for (const QString &sql : statements) {
        if (!execOne(db, sql, error))
            return false;
    }
    return true;
}

} // namespace

// 返回默认数据库文件位置
QString defaultDatabasePath()
{
    return QDir::current().filePath(QStringLiteral("lab09_telemetry.sqlite"));
}

// 保证数据库文件的父目录存在
bool ensureParentDir(const QString &path, QString *error)
{
    const QFileInfo info(path);
    const QString dir = info.absolutePath();
    if (dir.isEmpty() || QDir().mkpath(dir))
        return true;
    if (error)
        *error = QStringLiteral("cannot create directory: %1").arg(dir);
    return false;
}

// 配置“写连接”（采集会话、写样本用的那条连接）
bool configureWriter(QSqlDatabase &db, QString *error, int *repairedSessions)
{
    const QStringList pragmas = {
        QStringLiteral("PRAGMA journal_mode = WAL"),    // 读并发基础，
        QStringLiteral("PRAGMA synchronous = NORMAL"),
        QStringLiteral("PRAGMA busy_timeout = 5000"),
        QStringLiteral("PRAGMA foreign_keys = ON"),
    };
    // 执行基础配置
    if (!execAll(db, pragmas, error))
        return false;

    // 建立表结构
    if (!ensureSchema(db, error))
        return false;

    // 进程被强杀时来不及写 ended_ms。下次启动把所有“未结束”的会话修好：
    // 结束时间取该会话最后一个样本（没有样本则取开始时间），
    QSqlQuery repair(db);
    if (!repair.exec(QStringLiteral(
            "UPDATE sessions SET"
            "  ended_ms = COALESCE((SELECT MAX(ts_ms) FROM samples"
            "                        WHERE samples.session_id = sessions.id), started_ms),"
            "  sample_count = (SELECT COUNT(*) FROM samples"
            "                  WHERE samples.session_id = sessions.id)"
            " WHERE ended_ms IS NULL"))) {
        if (error)
            *error = QStringLiteral("cannot repair unfinished sessions: %1")
                         .arg(describeError(repair.lastError()));
        return false;
    }
    // 没有待修复的会话时 numRowsAffected() 为 0，属正常情况。
    if (repairedSessions)
        *repairedSessions = repair.numRowsAffected();

    return true;
}

// 配置“读连接”（列表查询、分页浏览用的连接）
bool configureReader(QSqlDatabase &db, QString *error)
{
    const QStringList pragmas = {
        QStringLiteral("PRAGMA busy_timeout = 5000"),
        QStringLiteral("PRAGMA foreign_keys = ON"),
    };
    return execAll(db, pragmas, error);
}

// 建表/迁移入口
//   version > kSchemaVersion → 库比程序新，拒绝启动，避免旧代码写坏新结构；
//   version < kSchemaVersion → 逐级迁移（此处只有 0 → 1）。
bool ensureSchema(QSqlDatabase &db, QString *error)
{
    // 创建一个绑定在该连接上的查询对象
    QSqlQuery versionQuery(db);
    // 读库结构版本号，exec 返回是否有结果集，next() 把游标移到第一行
    if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) || !versionQuery.next())
    {
        // 读不到版本号就无法判断该不该迁移，直接失败
        return false;
    }

    // 取第 0 列,获取当前 schema 版本
    const int version = versionQuery.value(0).toInt();

    // 库版本比程序新：说明是旧程序打开了新库
    if (version > kSchemaVersion) {
        // 拒绝继续：旧代码按旧结构写会破坏新库，宁可不启动
        return false;
    }

    // 全新库 user_version 默认是 0，走初始化分支
    // （将来 v2 时这里会变成 if (version < 2) 并追加 1→2 的迁移块）
    if (version < 1) {
        // 把多条语句收集成列表，交给 execAll 顺序执行
        const QStringList statements = {
            // 建会话表
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS sessions ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  label TEXT NOT NULL,"
                "  started_ms INTEGER NOT NULL,"
                "  ended_ms INTEGER,"
                "  sample_count INTEGER NOT NULL DEFAULT 0"
                ")"),
            // 建样本表
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS samples ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  session_id INTEGER NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,"
                "  ts_ms INTEGER NOT NULL,"
                "  channel INTEGER NOT NULL,"
                "  value REAL NOT NULL,"
                "  quality INTEGER NOT NULL"
                ")"),
            // 建立索引
            // 分页查询按 (session_id, ts_ms, id) 排序，这条索引同时覆盖
            // WHERE session_id = ? 和 ORDER BY ts_ms, id，避免全表扫描 + 排序。
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_samples_session_time"
                " ON samples(session_id, ts_ms, id)"),
            // 预留给“按时间清理旧数据”和跨会话时间查询。
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_samples_time ON samples(ts_ms)"),
            QStringLiteral("PRAGMA user_version = 1"),
        };
        if (!execAll(db, statements, error))
            return false;
    }

    return true;
}

// 把 QSqlError 拼成一行可读文本
QString describeError(const QSqlError &error)
{
    if (!error.isValid())
        return QStringLiteral("no error");

    QString text = error.databaseText();
    if (text.isEmpty())
        text = error.driverText();
    else if (!error.driverText().isEmpty() && error.driverText() != text)
        text += QStringLiteral(" | ") + error.driverText();

    if (error.nativeErrorCode().size() > 0)
        text += QStringLiteral(" (native=%1)").arg(error.nativeErrorCode());
    return text;
}

} // namespace db