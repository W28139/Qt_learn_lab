#include "dbwriter.h"

#include "databaseschema.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QTimer>

#include <utility>

namespace {

constexpr int kFlushIntervalMs = 1000; // 最长 1 秒提交一次
constexpr int kFlushRows = 500;        // 或缓冲到 500 行就先提交
const char kConnectionName[] = "lab10_writer";

} // namespace

DbWriter::DbWriter(QString dbPath, QObject *parent)
    : QObject(parent)
    , m_path(std::move(dbPath))
{
    // 构造函数在 GUI 线程执行：这里不创建连接，只保存路径。
    // 连接要等 open() 在 DB 线程执行时再建，线程亲和性才正确。
}

DbWriter::~DbWriter()
{
    // 正常路径上 shutdown() 已经清理过；这里兜底，避免连接泄漏。
    m_insert.reset();
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();  // 让本地 QSqlDatabase handle 失效，因为复制出来的 handle 共享同一底层连接
    //  移除连接名，避免 QSqlDatabase 全局连接表里残留条目。
    if (QSqlDatabase::contains(QString::fromLatin1(kConnectionName)))
        QSqlDatabase::removeDatabase(QString::fromLatin1(kConnectionName));
}

void DbWriter::open()
{
    // 幂等：已经成功打开过就直接返回。
    if (m_opened)
        return;

    // 检查 Qt 是否部署了 SQLite 驱动插件。
    if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        emit opened(m_path, QStringLiteral("SQLite driver (QSQLITE) is not available; "
                                           "check the Qt Sql module and plugin deployment"));
        return;
    }

    // 确保数据库文件所在父目录存在。
    QString error;
    if (!db::ensureParentDir(m_path, &error)) {
        emit opened(m_path, error);
        return;
    }

    // addDatabase() 必须在 DB 线程执行。
    // 连接名进程内唯一；如果残留同名连接，先移除，保证可重复 open()。
    const QString connectionName = QString::fromLatin1(kConnectionName);
    if (QSqlDatabase::contains(connectionName))
        QSqlDatabase::removeDatabase(connectionName);

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    m_db.setDatabaseName(m_path);

    // 打开失败：清空 handle 并移除连接名，避免连接泄漏。
    if (!m_db.open()) {
        error = QStringLiteral("cannot open database: %1")
        .arg(db::describeError(m_db.lastError()));
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
        emit opened(m_path, error);
        return;
    }

    // 设置写连接 PRAGMA、建表/校验 schema，并修复上次崩溃留下的未完成会话。
    int repairedSessions = 0;
    if (!db::configureWriter(m_db, &error, &repairedSessions)) {
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
        emit opened(m_path, error);
        return;
    }

    // 预编译 INSERT：整个连接生命周期复用，flush 时只 bind + exec。
    m_insert = std::make_unique<QSqlQuery>(m_db);
    if (!m_insert->prepare(QStringLiteral(
            "INSERT INTO samples(session_id, ts_ms, channel, value, quality)"
            " VALUES (?, ?, ?, ?, ?)"))) {
        error = QStringLiteral("cannot prepare INSERT: %1")
        .arg(db::describeError(m_insert->lastError()));
        m_insert.reset();
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
        emit opened(m_path, error);
        return;
    }

    // flush 定时器在 DB 线程创建，timeout 也在 DB 线程触发 flush()。
    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(kFlushIntervalMs);
    connect(m_flushTimer, &QTimer::timeout, this, &DbWriter::flush);
    m_flushTimer->start();

    // 标记打开成功并通知上层。
    m_opened = true;
    emit logMessage(QStringLiteral("database opened: %1 (WAL, synchronous=NORMAL)")
                        .arg(m_path));
    if (repairedSessions > 0) {
        emit logMessage(QStringLiteral("repaired %1 unfinished session(s) from a previous crash")
                            .arg(repairedSessions));
    }
    emit opened(m_path, QString());
}

void DbWriter::beginSession()
{
    if (!m_opened) {
        emit errorOccurred(QStringLiteral("database is not open"));
        return;
    }

    // 正常情况下调用方会先 stop；这里兜底，确保旧会话有结束时间。
    if (m_sessionId != 0)
        endSession();

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const QString label =
        QDateTime::fromMSecsSinceEpoch(nowMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO sessions(label, started_ms, ended_ms, sample_count)"
        " VALUES (?, ?, NULL, 0)"));
    query.addBindValue(label);
    query.addBindValue(nowMs);
    if (!query.exec()) {
        emit errorOccurred(QStringLiteral("cannot insert session: %1")
                               .arg(db::describeError(query.lastError())));
        return;
    }

    m_sessionId = query.lastInsertId().toLongLong();
    m_sessionRows = 0;
    emit sessionStarted(m_sessionId, nowMs);
    emit logMessage(QStringLiteral("session %1 started (%2)").arg(m_sessionId).arg(label));
}

void DbWriter::appendSamples(const QList<TelemetrySample> &samples)
{
    if (samples.isEmpty())
        return;

    if (!m_opened || m_sessionId == 0) {
        emit errorOccurred(QStringLiteral("received %1 samples without an active session")
                               .arg(samples.size()));
        return;
    }

    m_pending.append(samples);
    emitStats();

    if (m_pending.size() >= kFlushRows)
        flush();
}

void DbWriter::flush()
{
    writePending();
}

bool DbWriter::writePending()
{
    if (m_pending.isEmpty())
        return true;

    if (!m_opened || m_sessionId == 0) {
        emit errorOccurred(QStringLiteral("cannot flush without an open database/session"));
        m_pending.clear();
        emitStats();
        return false;
    }

    // 先把本批移出缓冲：提交成功后它就是已写入数据；
    // 提交失败则整批回滚并丢弃（真实项目应转入重试/落盘补写）。
    const QList<TelemetrySample> batch = std::move(m_pending);

    QElapsedTimer timer;
    timer.start();

    if (!m_db.transaction()) {
        emit errorOccurred(QStringLiteral("BEGIN failed: %1")
                               .arg(db::describeError(m_db.lastError())));
        emitStats();
        return false;
    }

    bool ok = true;
    QString failure;
    for (const TelemetrySample &sample : batch) {
        m_insert->bindValue(0, m_sessionId);
        m_insert->bindValue(1, sample.timestampMs);
        m_insert->bindValue(2, static_cast<int>(sample.channel));
        m_insert->bindValue(3, sample.value);
        m_insert->bindValue(4, static_cast<int>(sample.quality));

        if (!m_insert->exec()) {
            ok = false;
            failure = db::describeError(m_insert->lastError());
            break;
        }
        // 复用预编译语句：每次 exec 后 finish()，释放本轮结果资源。
        m_insert->finish();
    }

    if (ok && !m_db.commit()) {
        ok = false;
        failure = QStringLiteral("COMMIT failed: %1")
                      .arg(db::describeError(m_db.lastError()));
    }

    if (!ok) {
        m_db.rollback();
        emit errorOccurred(QStringLiteral("batch of %1 rows rolled back: %2")
                               .arg(batch.size())
                               .arg(failure));
        emitStats();
        return false;
    }

    m_rowsWritten += static_cast<quint64>(batch.size());
    m_sessionRows += batch.size();
    ++m_commits;
    m_lastCommitUs = timer.nsecsElapsed() / 1000;
    emitStats();
    return true;
}

void DbWriter::endSession()
{
    if (m_sessionId == 0)
        return;

    // 先把缓冲写完，再记录会话统计；否则 sample_count 会少一批。
    flush();

    const qint64 sessionId = m_sessionId;
    const qint64 rows = m_sessionRows;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE sessions SET ended_ms = ?, sample_count = ? WHERE id = ?"));
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    query.addBindValue(rows);
    query.addBindValue(sessionId);
    if (!query.exec()) {
        emit errorOccurred(QStringLiteral("cannot finish session %1: %2")
                               .arg(sessionId)
                               .arg(db::describeError(query.lastError())));
    }

    m_sessionId = 0;
    m_sessionRows = 0;
    emit sessionFinished(sessionId, rows);
    emit logMessage(QStringLiteral("session %1 finished, %2 rows").arg(sessionId).arg(rows));
}

void DbWriter::clearAll()
{
    if (!m_opened) {
        emit errorOccurred(QStringLiteral("database is not open"));
        return;
    }
    if (m_sessionId != 0) {
        emit errorOccurred(QStringLiteral("stop the current session before clearing history"));
        return;
    }

    if (!m_db.transaction()) {
        emit errorOccurred(QStringLiteral("BEGIN failed: %1")
                               .arg(db::describeError(m_db.lastError())));
        return;
    }

    bool ok = true;
    QString failure;
    const QStringList statements = {
        QStringLiteral("DELETE FROM samples"),
        QStringLiteral("DELETE FROM sessions"),
        // AUTOINCREMENT 的计数器也复位，下次会话从 #1 开始。
        QStringLiteral("DELETE FROM sqlite_sequence WHERE name IN ('sessions', 'samples')"),
    };
    for (const QString &sql : statements) {
        QSqlQuery query(m_db);
        if (!query.exec(sql)) {
            ok = false;
            failure = db::describeError(query.lastError());
            break;
        }
    }

    if (ok && !m_db.commit()) {
        ok = false;
        failure = db::describeError(m_db.lastError());
    }
    if (!ok) {
        m_db.rollback();
        emit errorOccurred(QStringLiteral("cannot clear history: %1").arg(failure));
        return;
    }

    m_rowsWritten = 0;
    m_commits = 0;
    m_lastCommitUs = 0;
    emitStats();
    emit cleared();
    emit logMessage(QStringLiteral("history cleared"));
}

void DbWriter::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;

    if (m_flushTimer) {
        m_flushTimer->stop();
        m_flushTimer->deleteLater();
        m_flushTimer = nullptr;
    }

    if (m_opened) {
        endSession(); // 内部会 flush

        // 释放顺序很重要：先销毁所有 QSqlQuery，再 close，再丢弃 handle，最后 remove。
        m_insert.reset();
        if (m_db.isOpen())
            m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(QString::fromLatin1(kConnectionName));
        m_opened = false;
        emit logMessage(QStringLiteral("database closed: rows=%1 commits=%2")
                            .arg(m_rowsWritten)
                            .arg(m_commits));
    }

    // 由 DB 线程自己结束事件循环，避免调用方抢先 quit()。
    if (QThread *thread = QThread::currentThread())
        thread->quit();
}

void DbWriter::emitStats()
{
    emit statsChanged(m_rowsWritten, m_commits, m_lastCommitUs,
                      static_cast<qint64>(m_pending.size()));
}