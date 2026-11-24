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
const char kConnectionName[] = "lab09_writer";

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

    // 设置写连接 PRAGMA、建表/校验 schema。
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
            " VALUES (?, ?, ?, ?, ?)")))
    {
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
    // 前置检查：数据库必须已打开，否则直接报错返回
    if (!m_opened) {
        emit errorOccurred(QStringLiteral("database is not open"));
        return;
    }

    // 正常情况下调用方会先 stop；这里兜底，确保旧会话有结束时间。
    if (m_sessionId != 0)
        endSession();

    // 当前时间戳（毫秒），同时用作 started_ms 和默认 label
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const QString label =
        QDateTime::fromMSecsSinceEpoch(nowMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    // 插入一条新会话记录：ended_ms 置 NULL 表示进行中，sample_count 初始为 0
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

    // 记录新会话的 id，重置本会话已写入的行数计数
    m_sessionId = query.lastInsertId().toLongLong();
    m_sessionRows = 0;

    // 对外通知：会话已开始，并记录一条日志
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
    // 没有活动会话，直接返回（幂等，可安全重复调用）
    if (m_sessionId == 0)
        return;

    // 先把缓冲写完，再记录会话统计；否则 sample_count 会少一批。
    flush();

    // 先把局部变量存下来：后面要重置成员，但报错/回调还得用原值
    const qint64 sessionId = m_sessionId;
    const qint64 rows = m_sessionRows;

    // 更新会话记录：写入结束时间和实际行数，标记该会话已结束
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE sessions SET ended_ms = ?, sample_count = ? WHERE id = ?"));
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    query.addBindValue(rows);
    query.addBindValue(sessionId);
    if (!query.exec()) {
        // 注意：即便写失败，下面仍会清状态并通知，避免卡在"半开会话"状态
        emit errorOccurred(QStringLiteral("cannot finish session %1: %2")
                               .arg(sessionId)
                               .arg(db::describeError(query.lastError())));
    }

    // 清理会话状态：id 归零表示当前无活动会话，行数计数清零待下次会话
    m_sessionId = 0;
    m_sessionRows = 0;

    // 对外通知：会话已结束（带 id 和最终行数），并记一条日志
    emit sessionFinished(sessionId, rows);
    emit logMessage(QStringLiteral("session %1 finished, %2 rows").arg(sessionId).arg(rows));
}

void DbWriter::clearAll()
{
    // 前置检查：数据库必须已打开
    if (!m_opened) {
        emit errorOccurred(QStringLiteral("database is not open"));
        return;
    }
    // 还有活动会话时禁止清空，否则会留下"孤儿"会话/统计错乱
    if (m_sessionId != 0) {
        emit errorOccurred(QStringLiteral("stop the current session before clearing history"));
        return;
    }

    // 开启事务：三条 DELETE 要么全成功，要么全回滚
    if (!m_db.transaction()) {
        emit errorOccurred(QStringLiteral("BEGIN failed: %1")
                               .arg(db::describeError(m_db.lastError())));
        return;
    }

    // 依次执行删除：先删明细(samples)再删主表(sessions)，最后复位自增计数器
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
            break;   // 任一失败即停止，交给下面统一回滚
        }
    }

    // 语句都成功才提交；提交本身也可能失败（如磁盘满、锁冲突）
    if (ok && !m_db.commit()) {
        ok = false;
        failure = db::describeError(m_db.lastError());
    }
    if (!ok) {
        m_db.rollback();   // 回滚保证数据库回到清空前的状态
        emit errorOccurred(QStringLiteral("cannot clear history: %1").arg(failure));
        return;
    }

    // 库清空后，内存里的统计也要同步复位，避免显示与实际不一致
    m_rowsWritten = 0;
    m_commits = 0;
    m_lastCommitUs = 0;
    emitStats();                         // 刷新统计信号（周期/吞吐等归零）
    emit cleared();                      // 通知外界历史已清
    emit logMessage(QStringLiteral("history cleared"));
}

void DbWriter::shutdown()
{
    // 幂等保护：重复调用（如析构 + 显式 shutdown）直接返回，避免二次释放
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;

    // 先停掉周期性 flush 定时器：防止关库后还有回调触发 flush 访问已销毁的 m_db
    if (m_flushTimer) {
        m_flushTimer->stop();
        m_flushTimer->deleteLater();   // 用 deleteLater 而非 delete，避免跨线程/同栈删除风险
        m_flushTimer = nullptr;
    }

    if (m_opened) {
        endSession(); // 内部会 flush，把缓冲数据落盘并结束未关会话

        // 释放顺序很重要：先销毁所有 QSqlQuery，再 close，再丢弃 handle，最后 remove。
        // 顺序反了（例如先 close 再 reset query）会导致 Qt 警告
        // "connection is still in use" 或查询内部仍持有失效连接。
        m_insert.reset();              // 清掉常驻的预编译 INSERT 语句
        if (m_db.isOpen())
            m_db.close();
        m_db = QSqlDatabase();         // 丢弃本地 handle，让连接引用计数归零
        QSqlDatabase::removeDatabase(QString::fromLatin1(kConnectionName));  // 从 Qt 连接池移除
        m_opened = false;
        emit logMessage(QStringLiteral("database closed: rows=%1 commits=%2")
                            .arg(m_rowsWritten)
                            .arg(m_commits));
    }

    // 由 DB 线程自己结束事件循环，避免调用方抢先 quit()。
    // 在 DB 线程内调用 quit() 是最稳妥的：外层可能还排在队列里的事件
    // （如最后的 flush/信号）能正常处理完，也不会出现"线程已 quit 又被外部操作"的竞态。
    if (QThread *thread = QThread::currentThread())
        thread->quit();
}

void DbWriter::emitStats()
{
    emit statsChanged(m_rowsWritten, m_commits, m_lastCommitUs,
                      static_cast<qint64>(m_pending.size()));
}