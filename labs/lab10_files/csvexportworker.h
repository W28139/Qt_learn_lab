#pragma once

#include <QAtomicInt>
#include <QObject>
#include <QString>

// 在独立线程里把一个会话导出成 CSV。
//
// 为什么单独开线程：导出要读数据库、写文件、计算转义，几万行就可能
// 让 GUI 卡住；线程里用自己的只读连接，不碰 GUI 的读连接和 writer。
class CsvExportWorker final : public QObject
{
    Q_OBJECT

public:
    explicit CsvExportWorker(QString dbPath, QObject *parent = nullptr);
    ~CsvExportWorker() override;

    // 线程安全的取消标记。可以直接从 GUI 线程调用：
    // 导出槽正在阻塞执行时，队列连接投递的槽不会被处理，
    // 但原子变量可以随时被另一个线程改。
    void cancel() { m_cancel.storeRelease(1); }

public slots:
    void exportSession(qint64 sessionId, const QString &outputPath, bool utf8Bom);

signals:
    void progress(qint64 done, qint64 total);
    void finished(bool ok, qint64 rows, const QString &path, const QString &error);
    void logMessage(const QString &text);

private:
    QString m_dbPath;
    QAtomicInt m_cancel{0};
};