#include "settingsdialog.h"

#include "appsettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

void selectData(QComboBox *combo, const QVariant &data)
{
    const int index = combo->findData(data);
    if (index >= 0)
        combo->setCurrentIndex(index);
}

} // namespace

SettingsDialog::SettingsDialog(AppSettings &settings,
                               const QString &currentDbPath,
                               const QString &currentExportDir,
                               QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(QStringLiteral("设置"));
    resize(620, 300);

    m_dbPathEdit = new QLineEdit(currentDbPath, this);
    m_exportDirEdit = new QLineEdit(currentExportDir, this);

    auto *dbBrowse = new QPushButton(QStringLiteral("浏览…"), this);
    auto *exportBrowse = new QPushButton(QStringLiteral("浏览…"), this);

    auto *dbRow = new QHBoxLayout;
    dbRow->setContentsMargins(0, 0, 0, 0);
    dbRow->addWidget(m_dbPathEdit, 1);
    dbRow->addWidget(dbBrowse);

    auto *exportRow = new QHBoxLayout;
    exportRow->setContentsMargins(0, 0, 0, 0);
    exportRow->addWidget(m_exportDirEdit, 1);
    exportRow->addWidget(exportBrowse);

    m_pageSizeCombo = new QComboBox(this);
    m_pageSizeCombo->addItem(QStringLiteral("50 行"), 50);
    m_pageSizeCombo->addItem(QStringLiteral("100 行"), 100);
    m_pageSizeCombo->addItem(QStringLiteral("200 行"), 200);
    m_pageSizeCombo->addItem(QStringLiteral("500 行"), 500);
    selectData(m_pageSizeCombo, settings.pageSize(100));

    m_logLevelCombo = new QComboBox(this);
    m_logLevelCombo->addItem(QStringLiteral("Debug（全部，含帧级日志）"),
                             QStringLiteral("debug"));
    m_logLevelCombo->addItem(QStringLiteral("Info（推荐）"), QStringLiteral("info"));
    m_logLevelCombo->addItem(QStringLiteral("Warning（只看警告）"),
                             QStringLiteral("warning"));
    m_logLevelCombo->addItem(QStringLiteral("Critical（只看严重错误）"),
                             QStringLiteral("critical"));
    selectData(m_logLevelCombo, settings.logLevel(QStringLiteral("info")));

    m_autoStartCheck = new QCheckBox(QStringLiteral("启动后自动开始采集"), this);
    m_autoStartCheck->setChecked(settings.autoStart(true));

    m_bomCheck = new QCheckBox(QStringLiteral("CSV 写入 UTF-8 BOM（Excel 打开中文不乱码）"),
                               this);
    m_bomCheck->setChecked(settings.utf8Bom(true));

    auto *form = new QFormLayout;
    form->setSpacing(8);
    form->addRow(QStringLiteral("数据库路径："), dbRow);
    form->addRow(QString(), new QLabel(
        QStringLiteral("重启后生效；命令行 --db 优先于这里。"), this));
    form->addRow(QStringLiteral("CSV 导出目录："), exportRow);
    form->addRow(QStringLiteral("历史页每页行数："), m_pageSizeCombo);
    form->addRow(QStringLiteral("日志级别："), m_logLevelCombo);
    form->addRow(QString(), m_autoStartCheck);
    form->addRow(QString(), m_bomCheck);

    auto *saveButton = new QPushButton(QStringLiteral("保存"), this);
    auto *cancelButton = new QPushButton(QStringLiteral("取消"), this);
    saveButton->setDefault(true);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(saveButton);
    buttons->addWidget(cancelButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form, 1);
    layout->addLayout(buttons);

    connect(dbBrowse, &QPushButton::clicked, this, &SettingsDialog::browseDatabase);
    connect(exportBrowse, &QPushButton::clicked, this, &SettingsDialog::browseExportDir);
    connect(saveButton, &QPushButton::clicked, this, &SettingsDialog::save);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

QString SettingsDialog::dbPath() const
{
    return QFileInfo(m_dbPathEdit->text().trimmed()).absoluteFilePath();
}

QString SettingsDialog::exportDir() const
{
    return QDir(m_exportDirEdit->text().trimmed()).absolutePath();
}

int SettingsDialog::pageSize() const
{
    return m_pageSizeCombo->currentData().toInt();
}

QString SettingsDialog::logLevel() const
{
    return m_logLevelCombo->currentData().toString();
}

bool SettingsDialog::autoStart() const
{
    return m_autoStartCheck->isChecked();
}

bool SettingsDialog::utf8Bom() const
{
    return m_bomCheck->isChecked();
}

void SettingsDialog::browseDatabase()
{
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("选择 SQLite 数据库文件"),
        m_dbPathEdit->text(),
        QStringLiteral("SQLite 数据库 (*.sqlite *.db);;所有文件 (*)"));
    if (!path.isEmpty())
        m_dbPathEdit->setText(path);
}

void SettingsDialog::browseExportDir()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择 CSV 导出目录"), m_exportDirEdit->text());
    if (!dir.isEmpty())
        m_exportDirEdit->setText(dir);
}

void SettingsDialog::save()
{
    if (m_dbPathEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("设置"),
                             QStringLiteral("数据库路径不能为空。"));
        return;
    }
    if (m_exportDirEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("设置"),
                             QStringLiteral("导出目录不能为空。"));
        return;
    }
    if (!QDir().mkpath(m_exportDirEdit->text().trimmed())) {
        QMessageBox::warning(
            this, QStringLiteral("设置"),
            QStringLiteral("无法创建导出目录：%1").arg(m_exportDirEdit->text()));
        return;
    }

    m_settings.setDbPath(dbPath());
    m_settings.setExportDir(exportDir());
    m_settings.setPageSize(pageSize());
    m_settings.setLogLevel(logLevel());
    m_settings.setAutoStart(autoStart());
    m_settings.setUtf8Bom(utf8Bom());

    QString error;
    if (!m_settings.sync(&error)) {
        QMessageBox::critical(this, QStringLiteral("设置"), error);
        return;
    }

    accept();
}