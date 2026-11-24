#pragma once

#include <QString>

// 本 Lab 的文件布局集中在这里。
//
// 默认使用 QStandardPaths 给出的“标准位置”：
//   Linux   ~/.local/share/QtLearn/lab10_files 等
//   Windows %LOCALAPPDATA%\QtLearn\lab10_files 等
//
// --data-dir 覆盖时，所有数据、配置、日志、导出都放在同一个目录下，
// 方便实验和无头验证，不污染真实配置。
namespace apppaths {

struct Paths
{
    QString dataDir;     // 数据库等程序数据
    QString configDir;   // 配置文件
    QString logDir;      // 日志
    QString exportDir;   // CSV 导出
    QString dbPath;      // dataDir/telemetry.sqlite
    QString settingsFile; // configDir/settings.ini
    QString logFile;     // logDir/app.log
};

// 计算路径并创建目录。失败时返回空结构，error 写入原因。
Paths resolve(const QString &dataDirOverride, QString *error);

// 创建 Paths 里所有需要的目录。幂等。
bool ensureDirs(const Paths &paths, QString *error);

} // namespace apppaths