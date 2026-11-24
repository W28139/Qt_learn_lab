#pragma once

#include <QByteArray>
#include <QSettings>
#include <QString>

// 对 QSettings 的薄封装：所有键名集中在这里，避免界面、日志、导出
// 各自拼字符串。
//
// 明确使用 IniFormat：文件是普通文本，方便课程里直接打开检查；
// Windows 上也不会悄悄写进注册表。
class AppSettings
{
public:
    explicit AppSettings(const QString &filePath);

    QString dbPath(const QString &fallback) const;
    void setDbPath(const QString &value);

    QString exportDir(const QString &fallback) const;
    void setExportDir(const QString &value);

    int pageSize(int fallback) const;
    void setPageSize(int value);

    bool utf8Bom(bool fallback) const;
    void setUtf8Bom(bool value);

    QString logLevel(const QString &fallback) const;
    void setLogLevel(const QString &value);

    bool autoStart(bool fallback) const;
    void setAutoStart(bool value);

    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &value);

    QByteArray windowState() const;
    void setWindowState(const QByteArray &value);

    // 写入磁盘并检查 QSettings::status()。成功返回 true。
    bool sync(QString *error);

    QString fileName() const { return m_settings.fileName(); }

private:
    QSettings m_settings;
};