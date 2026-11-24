#include "csvexportworker.h"

#include "databaseschema.h"
#include "logging.h"
#include "telemetrytypes.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include <utility>

namespace {

constexpr int kCheckCancelEvery = 256;
constexpr int kProgressEvery = 512;
constexpr int kBufferFlushBytes = 64 * 1024;

QString connectionNameFor(const void *pointer)
{
    return QStringLiteral("lab10_csv_%1")
        .arg(reinterpret_cast<quintptr>(pointer), 0, 16);
}

// RFC 4180 的转义：字段里有逗号、双引号或换行时整体加引号，
// 内部的双引号写成两个双引号。
QString csvEscape(const QString &value)
{
    const bool needsQuotes = value.contains(QLatin1Char(','))
                             || value.contains(QLatin1Char('"'))
                             || value.contains(QLatin1Char('\n'))
                             || value.contains(QLatin1Char('\r'));
    if (!needsQuotes)
        return value;

    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QByteArray csvRow(const QStringList &fields)
{
    QStringList escaped;
    escaped.reserve(fields.size());
    for (const QString &field : fields)
        escaped.append(csvEscape(field));
    return (escaped.join(QLatin1Char(',')) + QStringLiteral("\r\n")).toUtf8();
}

bool writeAll(QSaveFile &file, const QByteArray &bytes, QString *error)
{
    if (file.write(bytes) == bytes.size())
        return true;
    if (error)
        *error = QStringLiteral("cannot write CSV: %1").arg(file.errorString());
    return false;
}

} // namespace

CsvExportWorker::CsvExportWorker(QString dbPath, QObject *parent)
    : QObject(parent)
    , m_dbPath(std::move(dbPath))
{
}

CsvExportWorker::~CsvExportWorker() = default;

void CsvExportWorker::exportSession(qint64 sessionId, const QString &outputPath, bool utf8Bom)
{
    m_cancel.storeRelease(0);

    QString error;
    qint64 written = 0;
    qint64 total = 0;
    bool canceled = false;

    const QString connectionName = connectionNameFor(this);
    if (QSqlDatabase::contains(connectionName))
        QSqlDatabase::removeDatabase(connectionName);

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(m_dbPath);
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));

    QSaveFile file(outputPath);

    do {
        if (!QDir().mkpath(QFileInfo(outputPath).absolutePath())) {
            error = QStringLiteral("cannot create export directory: %1")
                        .arg(QFileInfo(outputPath).absolutePath());
            break;
        }

        if (!db.open()) {
            error = QStringLiteral("cannot open database: %1")
                        .arg(db::describeError(db.lastError()));
            break;
        }

        {
            QSqlQuery sessionQuery(db);
            sessionQuery.prepare(QStringLiteral("SELECT label FROM sessions WHERE id = ?"));
            sessionQuery.addBindValue(sessionId);
            if (!sessionQuery.exec()) {
                error = QStringLiteral("session lookup failed: %1")
                            .arg(db::describeError(sessionQuery.lastError()));
                break;
            }
            if (!sessionQuery.next()) {
                error = QStringLiteral("session #%1 does not exist").arg(sessionId);
                break;
            }
        }

        {
            QSqlQuery countQuery(db);
            countQuery.prepare(
                QStringLiteral("SELECT COUNT(*) FROM samples WHERE session_id = ?"));
            countQuery.addBindValue(sessionId);
            if (!countQuery.exec() || !countQuery.next()) {
                error = QStringLiteral("row count failed: %1")
                            .arg(db::describeError(countQuery.lastError()));
                break;
            }
            total = countQuery.value(0).toLongLong();
        }

        emit progress(0, total);

        if (!file.open(QIODevice::WriteOnly)) {
            error = QStringLiteral("cannot open CSV for writing: %1").arg(file.errorString());
            break;
        }

        QByteArray buffer;
        if (utf8Bom)
            buffer.append(QByteArray("\xEF\xBB\xBF", 3));
        buffer.append(QStringLiteral("会话,时间,时间戳(ms),通道,数值,质量\r\n").toUtf8());

        // setForwardOnly(true) 让驱动只保留当前行，不把结果集缓存到内存。
        QSqlQuery query(db);
        query.setForwardOnly(true);
        query.prepare(QStringLiteral(
            "SELECT s.label, p.ts_ms, p.channel, p.value, p.quality"
            " FROM samples p JOIN sessions s ON s.id = p.session_id"
            " WHERE p.session_id = ?"
            " ORDER BY p.ts_ms, p.id"));
        query.addBindValue(sessionId);
        if (!query.exec()) {
            error = QStringLiteral("query failed: %1").arg(db::describeError(query.lastError()));
            break;
        }

        int sinceLastCheck = 0;
        while (query.next()) {
            if (++sinceLastCheck >= kCheckCancelEvery) {
                sinceLastCheck = 0;
                if (m_cancel.loadAcquire() != 0) {
                    canceled = true;
                    break;
                }
            }

            const QString label = query.value(0).toString();
            const qint64 tsMs = query.value(1).toLongLong();
            const int channel = query.value(2).toInt();
            const double value = query.value(3).toDouble();
            const Quality quality = static_cast<Quality>(query.value(4).toInt());

            const QString timeText = QDateTime::fromMSecsSinceEpoch(tsMs)
                                         .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
            buffer.append(csvRow({label,
                                  timeText,
                                  QString::number(tsMs),
                                  QString::number(channel),
                                  QString::number(value, 'f', 6),
                                  qualityText(quality)}));
            ++written;

            if (buffer.size() >= kBufferFlushBytes) {
                if (!writeAll(file, buffer, &error))
                    break;
                buffer.clear();
            }

            if (written % kProgressEvery == 0)
                emit progress(written, total);
        }

        if (!canceled && error.isEmpty() && !buffer.isEmpty()
            && !writeAll(file, buffer, &error)) {
            break;
        }
        if (canceled) {
            file.cancelWriting();
            error = QStringLiteral("已取消");
            break;
        }
        if (!error.isEmpty()) {
            file.cancelWriting();
            break;
        }

        if (!file.commit()) {
            error = QStringLiteral("cannot commit CSV: %1").arg(file.errorString());
            break;
        }
        emit progress(written, total);
    } while (false);

    db.close();
    db = QSqlDatabase();
    if (QSqlDatabase::contains(connectionName))
        QSqlDatabase::removeDatabase(connectionName);

    if (canceled) {
        emit logMessage(QStringLiteral("CSV export canceled after %1 rows: %2")
                            .arg(written)
                            .arg(outputPath));
    } else if (error.isEmpty()) {
        emit logMessage(QStringLiteral("CSV export finished: %1 rows -> %2")
                            .arg(written)
                            .arg(outputPath));
        qCInfo(lcCsv).noquote() << "export finished:" << written << "rows ->" << outputPath;
    } else {
        emit logMessage(QStringLiteral("CSV export failed: %1").arg(error));
        qCWarning(lcCsv).noquote() << "export failed:" << error;
    }

    emit finished(error.isEmpty(), written, outputPath, error);
}