#pragma once

#include "sessionstats.h"

#include <QMainWindow>

class QLabel;
class QTableWidget;

// 最小可发布窗口：生成示例数据、展示每通道统计、导出摘要 CSV。
// 业务逻辑集中在 SessionStats / csvutils 中，窗口仅负责展示。
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void generateSamples();   // 生成示例统计数据
    void exportSummary();     // 导出摘要 CSV 文件

private:
    void refreshTable();      // 根据 m_stats 刷新表格与汇总标签

    SessionStats m_stats;               // 统计数据模型
    QTableWidget *m_table = nullptr;    // 每通道统计表格
    QLabel *m_totalLabel = nullptr;     // 总计时长/计数汇总标签
    QLabel *m_buildLabel = nullptr;     // 构建信息标签
};