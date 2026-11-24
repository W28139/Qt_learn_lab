#include "appsettings.h"

namespace {

const char kDbPathKey[] = "storage/dbPath";
const char kExportDirKey[] = "export/dir";
const char kPageSizeKey[] = "history/pageSize";
const char kUtf8BomKey[] = "export/utf8Bom";
const char kLogLevelKey[] = "log/level";
const char kAutoStartKey[] = "capture/autoStart";
const char kGeometryKey[] = "window/geometry";
const char kStateKey[] = "window/state";

} // namespace

AppSettings::AppSettings(const QString &filePath)
    : m_settings(filePath, QSettings::IniFormat)
{
}

QString AppSettings::dbPath(const QString &fallback) const
{
    return m_settings.value(QString::fromLatin1(kDbPathKey), fallback).toString();
}

void AppSettings::setDbPath(const QString &value)
{
    m_settings.setValue(QString::fromLatin1(kDbPathKey), value);
}

QString AppSettings::exportDir(const QString &fallback) const
{
    return m_settings.value(QString::fromLatin1(kExportDirKey), fallback).toString();
}

void AppSettings::setExportDir(const QString &value)
{
    m_settings.setValue(QString::fromLatin1(kExportDirKey), value);
}

int AppSettings::pageSize(int fallback) const
{
    return m_settings.value(QString::fromLatin1(kPageSizeKey), fallback).toInt();
}

void AppSettings::setPageSize(int value)
{
    m_settings.setValue(QString::fromLatin1(kPageSizeKey), value);
}

bool AppSettings::utf8Bom(bool fallback) const
{
    return m_settings.value(QString::fromLatin1(kUtf8BomKey), fallback).toBool();
}

void AppSettings::setUtf8Bom(bool value)
{
    m_settings.setValue(QString::fromLatin1(kUtf8BomKey), value);
}

QString AppSettings::logLevel(const QString &fallback) const
{
    return m_settings.value(QString::fromLatin1(kLogLevelKey), fallback).toString();
}

void AppSettings::setLogLevel(const QString &value)
{
    m_settings.setValue(QString::fromLatin1(kLogLevelKey), value);
}

bool AppSettings::autoStart(bool fallback) const
{
    return m_settings.value(QString::fromLatin1(kAutoStartKey), fallback).toBool();
}

void AppSettings::setAutoStart(bool value)
{
    m_settings.setValue(QString::fromLatin1(kAutoStartKey), value);
}

QByteArray AppSettings::windowGeometry() const
{
    return m_settings.value(QString::fromLatin1(kGeometryKey)).toByteArray();
}

void AppSettings::setWindowGeometry(const QByteArray &value)
{
    m_settings.setValue(QString::fromLatin1(kGeometryKey), value);
}

QByteArray AppSettings::windowState() const
{
    return m_settings.value(QString::fromLatin1(kStateKey)).toByteArray();
}

void AppSettings::setWindowState(const QByteArray &value)
{
    m_settings.setValue(QString::fromLatin1(kStateKey), value);
}

bool AppSettings::sync(QString *error)
{
    m_settings.sync();

    switch (m_settings.status()) {
    case QSettings::NoError:
        return true;
    case QSettings::AccessError:
        if (error)
            *error = QStringLiteral("cannot write settings file: %1").arg(m_settings.fileName());
        return false;
    case QSettings::FormatError:
        if (error)
            *error = QStringLiteral("settings file has invalid format: %1")
                         .arg(m_settings.fileName());
        return false;
    }

    if (error)
        *error = QStringLiteral("unknown QSettings error");
    return false;
}