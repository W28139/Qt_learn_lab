#include "mainwindow.h"

#include "csvutils.h"
#include "telemetrytypes.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>

namespace {

constexpr int kChannels = 4;
constexpr int kSamplesPerChannel = 200;

QString buildType()
{
#ifdef QT_DEBUG
    return QStringLiteral("Debug");
#else
    return QStringLiteral("Release");
#endif
}

// 表格和导出的表头共用一份，避免两处顺序不一致。
QStringList summaryHeaders()
{
    return {QStringLiteral("通道"),
            QStringLiteral("样本数"),
            QStringLiteral("均值"),
            QStringLiteral("最小"),
            QStringLiteral("最大"),
            QStringLiteral("正常"),
            QStringLiteral("警告"),
            QStringLiteral("异常")};
}

// 一行摘要的 8 个字段；表格和 CSV 共用。
QStringList summaryRow(int channel, const ChannelStats &stats)
{
    return {QStringLiteral("CH %1").arg(channel),
            QString::number(stats.count),
            QString::number(stats.mean, 'f', 3),
            QString::number(stats.min, 'f', 3),
            QString::number(stats.max, 'f', 3),
            QString::number(stats.good),
            QString::number(stats.warning),
            QString::number(stats.bad)};
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Lab 11 - Qt Test 与发布打包"));
    resize(980, 560);

    m_buildLabel = new QLabel(
        QStringLiteral("构建：%1 | Qt %2").arg(buildType(), QString::fromLatin1(qVersion())),
        this);

    auto *generateButton = new QPushButton(QStringLiteral("生成示例数据"), this);
    auto *exportButton = new QPushButton(QStringLiteral("导出摘要 CSV…"), this);

    m_totalLabel = new QLabel(QStringLiteral("总样本：0"), this);

    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);
    toolbar->addWidget(generateButton);
    toolbar->addWidget(exportButton);
    toolbar->addSpacing(16);
    toolbar->addWidget(m_totalLabel);
    toolbar->addStretch();
    toolbar->addWidget(m_buildLabel);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels(summaryHeaders());
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    layout->addLayout(toolbar);
    layout->addWidget(m_table, 1);
    setCentralWidget(central);

    statusBar()->showMessage(
        QStringLiteral("测试：ctest --test-dir build/<preset> --output-on-failure；"
                       "打包：cmake --install build/<preset> --prefix dist"));

    connect(generateButton, &QPushButton::clicked, this, &MainWindow::generateSamples);
    connect(exportButton, &QPushButton::clicked, this, &MainWindow::exportSummary);

    refreshTable();
}

void MainWindow::generateSamples()
{
    m_stats.reset();

    // 固定公式、固定数量，保证每次生成的数据一致，方便对比测试结果。
    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
    for (int channel = 1; channel <= kChannels; ++channel) {
        for (int i = 0; i < kSamplesPerChannel; ++i) {
            TelemetrySample sample;
            sample.seq = static_cast<quint64>((channel - 1) * kSamplesPerChannel + i + 1);
            sample.timestampMs = startMs + static_cast<qint64>(i) * 50;
            sample.channel = static_cast<quint16>(channel);
            sample.value = 50.0 + channel * 10.0 + std::sin(i / 10.0 + channel) * 5.0;

            if (i % 97 == 96)
                sample.quality = Quality::Bad;
            else if (i % 37 == 36)
                sample.quality = Quality::Warning;
            else
                sample.quality = Quality::Good;

            m_stats.addSample(sample);
        }
    }

    refreshTable();
    statusBar()->showMessage(
        QStringLiteral("已生成 %1 个样本（%2 通道 × %3）")
            .arg(m_stats.totalCount())
            .arg(kChannels)
            .arg(kSamplesPerChannel),
        5000);
}

void MainWindow::refreshTable()
{
    const QList<int> channels = m_stats.channels();
    m_table->setRowCount(static_cast<int>(channels.size()));

    int row = 0;
    for (int channel : channels) {
        ChannelStats stats;
        if (!m_stats.channelStats(channel, &stats))
            continue;

        const QStringList values = summaryRow(channel, stats);

        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values.at(column));
            if (column > 0)
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(row, column, item);
        }
        ++row;
    }

    m_totalLabel->setText(QStringLiteral("总样本：%1").arg(m_stats.totalCount()));
}

void MainWindow::exportSummary()
{
    if (m_stats.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("导出摘要"),
                                 QStringLiteral("请先生成示例数据。"));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出每通道摘要"),
        QDir::homePath() + QStringLiteral("/lab11_summary.csv"),
        QStringLiteral("CSV 文件 (*.csv);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QStringLiteral("导出摘要"),
                             QStringLiteral("无法写入文件：%1").arg(file.errorString()));
        return;
    }

    QByteArray bytes("\xEF\xBB\xBF", 3); // UTF-8 BOM，方便 Excel 显示中文
    bytes += csv::toUtf8Line(summaryHeaders());

    const QList<int> channels = m_stats.channels();
    for (int channel : channels) {
        ChannelStats stats;
        if (!m_stats.channelStats(channel, &stats))
            continue;
        bytes += csv::toUtf8Line(summaryRow(channel, stats));
    }

    if (file.write(bytes) != bytes.size() || !file.commit()) {
        QMessageBox::warning(this, QStringLiteral("导出摘要"),
                             QStringLiteral("写入失败：%1").arg(file.errorString()));
        return;
    }

    statusBar()->showMessage(
        QStringLiteral("摘要已导出：%1").arg(QDir::toNativeSeparators(path)), 8000);
}