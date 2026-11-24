#pragma once

#include "telemetrytypes.h"

#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QtGlobal>

#include <memory>

class QSqlQuery;
class QTimer;

// 运行在独立 DB 线程里的写入器
// - 连接在 open() 里创建，而 open() 由 DB 线程执行，所以连接属于 DB 线程；
// - 所有公开槽都通过队列连接从 GUI 线程调用
// - 只有这个对象碰写连接

// 写入策略：appendSamples() 只把样本放进内存缓冲，达到行数阈值或flush 定时器到点后，用预编译语句批量提交
class DbWriter final : public QObject
{
    Q_OBJECT

public:
    // 保存数据库路径；父对象按 Qt 常规父子关系管理
    // 注意：构造时不应创建数据库连接，连接应在 DB 线程的 open() 中创建。（让连接亲和DB线程）
    explicit DbWriter(QString dbPath, QObject *parent = nullptr);
    // 析构时不应依赖跨线程直接关闭连接；
    // 正常收尾应通过 shutdown() 在 DB 线程中完成。（因为连接亲和DB线程）
    ~DbWriter() override;

public slots:
    // 在 DB 线程建连接、设置 pragma、建表/校验 schema。
    void open();

    // 新建一次采集会话，返回自增 id（通过 sessionStarted 信号）。
    void beginSession();

    // 向写缓冲追加一批样本。
    // 该槽只把数据放入 m_pending，不保证立即落盘；
    // 真正提交由 flush()触发
    void appendSamples(const QList<TelemetrySample> &samples);

    // 提交当前 m_pending 缓冲
    // 由 flush 定时器、endSession()、shutdown() 调用。
    void flush();

    // 结束当前会话：先 flush()，再写入会话结束时间和总样本行数。
    // 完成后通过 sessionFinished(sessionId, sampleCount) 通知。
    void endSession();

    // 清空所有会话与样本（仅在没有活动会话时允许）。
    void clearAll();

    // 收尾流程：
    // 1. flush() 提交剩余缓冲；
    // 2. endSession() 结束活动会话；
    // 3. 关闭数据库连接；
    // 4. 移除连接名；
    // 5. 结束 DB 线程事件循环。
    void shutdown();

signals:
    // 数据库打开/初始化结果。error 为空表示成功。
    void opened(const QString &path, const QString &error);

    // 新会话已开始，返回会话 id 和开始时间戳（毫秒）。
    void sessionStarted(qint64 sessionId, qint64 startedMs);

    // 会话已结束，返回会话 id 和该会话成功提交的样本行数。
    void sessionFinished(qint64 sessionId, qint64 sampleCount);

    // 写入统计变化：
    // rowsWritten  累计成功写入行数；
    // commits      累计提交事务次数；
    // lastCommitUs 最近一次提交耗时（微秒）；
    // pendingRows  当前尚未提交的缓冲行数。
    void statsChanged(quint64 rowsWritten, quint64 commits, qint64 lastCommitUs,
                      qint64 pendingRows);

    // clearAll() 成功完成。
    void cleared();

    // 发生错误时通知上层。
    void errorOccurred(const QString &text);

    // 普通日志消息，便于上层显示或记录。
    void logMessage(const QString &text);


private:
    // 将 m_pending 中的样本批量写入数据库。
    bool writePending();

    // 根据当前累计统计和 pending 行数发出 statsChanged()。
    void emitStats();

    // 数据库文件路径。
    QString m_path;

    // 仅在 DB 线程中使用。
    // QSqlDatabase 是值语义，复制出来的 handle 共享同一底层连接；
    QSqlDatabase m_db;

    // 预编译的 INSERT 语句，整个连接生命周期内复用，避免每次 flush 重新准备 SQL。
    std::unique_ptr<QSqlQuery> m_insert;

    // flush 定时器。应在 open() 中创建，使其亲和性属于 DB 线程；
    // 到点后触发 flush()，保证低流量时缓冲数据也能及时落盘。
    QTimer *m_flushTimer = nullptr;

    // 当前活动会话 id；0 表示当前没有活动会话。
    qint64 m_sessionId = 0;

    // 当前会话已经成功提交的样本行数，用于 sessionFinished() 上报。
    qint64 m_sessionRows = 0;

    // 尚未提交的样本缓冲。appendSamples() 只追加到这里，flush() 时批量写出。
    QList<TelemetrySample> m_pending;

    // 累计成功写入的样本行数。
    quint64 m_rowsWritten = 0;

    // 累计成功提交的事务次数。
    quint64 m_commits = 0;

    // 最近一次提交事务耗时，单位微秒。
    qint64 m_lastCommitUs = 0;

    // 数据库是否已成功打开并完成初始化。
    bool m_opened = false;

    // 是否正在执行 shutdown，用于防止重复收尾或拒绝新的写入操作。
    bool m_shuttingDown = false;
};