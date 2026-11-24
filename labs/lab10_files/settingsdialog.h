#pragma once

#include <QDialog>
#include <QString>

class AppSettings;
class QCheckBox;
class QComboBox;
class QLineEdit;

// 设置对话框：只负责编辑并写回 AppSettings，
// “哪些设置立即生效、哪些要重启”由 MainWindow 解释。
class SettingsDialog final : public QDialog
{
    Q_OBJECT

public:
    SettingsDialog(AppSettings &settings,
                   const QString &currentDbPath,
                   const QString &currentExportDir,
                   QWidget *parent = nullptr);

    // 仅在 exec() 返回 Accepted 后读取。
    QString dbPath() const;
    QString exportDir() const;
    int pageSize() const;
    QString logLevel() const;
    bool autoStart() const;
    bool utf8Bom() const;

private slots:
    void browseDatabase();
    void browseExportDir();
    void save();

private:
    AppSettings &m_settings;

    QLineEdit *m_dbPathEdit = nullptr;
    QLineEdit *m_exportDirEdit = nullptr;
    QComboBox *m_pageSizeCombo = nullptr;
    QComboBox *m_logLevelCombo = nullptr;
    QCheckBox *m_autoStartCheck = nullptr;
    QCheckBox *m_bomCheck = nullptr;
};