#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// 极小的 CSV 工具，给界面导出“每通道摘要”用。
// 纯函数，便于 Qt Test 做表驱动测试。
namespace csv {

// RFC 4180：字段含逗号、双引号或换行时整体加引号，
// 内部的双引号写成两个双引号。
QString escapeField(const QString &value);

// 逐字段转义、用逗号连接，并追加 CRLF。
QByteArray toUtf8Line(const QStringList &fields);

} // namespace csv