#pragma once

#include <QString>

class QSqlDatabase;
class QSqlError;

// 本 Lab 的数据库约定集中在这里：文件位置、连接名、pragma 和 schema。
// 注意：这些函数里不创建连接——连接必须由真正使用它的线程用
// QSqlDatabase::addDatabase() 创建。
namespace db {

QString defaultDatabasePath();

// 确保数据库文件所在目录存在。
bool ensureParentDir(const QString &path, QString *error);

// 写连接：WAL、busy_timeout、foreign_keys，并建表/校验 schema 版本。
// 必须在 writer 线程内、db.open() 之后调用。
// repairedSessions（可空）返回启动时修好的“上次异常退出遗留会话”数量。
bool configureWriter(QSqlDatabase &db, QString *error, int *repairedSessions = nullptr);

// 读连接：只需要 busy_timeout，读路径不修改 schema。
bool configureReader(QSqlDatabase &db, QString *error);

// 建表（幂等）并校验/更新 PRAGMA user_version。
bool ensureSchema(QSqlDatabase &db, QString *error);

// 把 QSqlError 合成一行人类可读文本，包含驱动文本、数据库文本和错误码。
QString describeError(const QSqlError &error);

} // namespace db