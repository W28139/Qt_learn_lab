#pragma once

#include <QLoggingCategory>
#include <QString>
#include <QtGlobal>

// 日志分类。qCInfo(lcCsv) 的输出会带上 "csv" 分类名，
// 可以用 QT_LOGGING_RULES 或 QLoggingCategory::setFilterRules() 单独开关。
Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcDb)
Q_DECLARE_LOGGING_CATEGORY(lcCsv)

namespace logging {

enum class Level {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Critical = 3,
};

// "debug"/"info"/"warning"/"critical"（大小写不敏感）。
// ok 为 false 表示字符串无法识别。
Level levelFromString(const QString &text, bool *ok = nullptr);
QString levelToString(Level level);

// 安装自定义消息处理器：
// - 所有 qDebug/qInfo/qWarning/qCritical 同时写 stderr 和 logDir/app.log；
// - 文件超过 maxBytes 或跨天时轮转，最多保留 maxFiles 个历史文件；
// - minimum 以下的级别直接丢弃；
// - 线程安全，可以从任意线程写日志。
bool install(const QString &logDir, Level minimum, qint64 maxBytes, int maxFiles,
             QString *error);

// 运行时调整级别（设置对话框会用到）。不改变文件位置。
void setMinimumLevel(Level minimum);
Level minimumLevel();

// 当前正在写的日志文件；未安装时返回空字符串。
QString currentLogFile();

// 刷新、关闭文件并恢复 Qt 默认处理器。应在主循环结束后调用。
void shutdown();

} // namespace logging