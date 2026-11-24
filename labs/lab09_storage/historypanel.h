#pragma once

#include <QSqlDatabase>
#include <QSqlQueryModel>
#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QSqlError;
class QTableView;

// 只读历史模型：在 QSqlQueryModel 基础上把数据库原始值格式化成界面文本。
class HistoryQueryModel final : public QSqlQueryModel
{
public:
    using QSqlQueryModel::QSqlQueryModel;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
};

// 历史分页面板。
//
// 它运行在 GUI 线程，用自己的只读连接查询 writer 写好的数据；
// writer 在 DB 线程用另一个连接写入，两者通过 WAL 并发。
class HistoryPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit HistoryPanel(QString dbPath, QWidget *parent = nullptr);
    ~HistoryPanel() override;

    // 必须在 GUI 线程调用；writer 已建好库之后再打开。
    bool open(QString *error);
    bool isOpen() const { return m_open; }

public slots:
    // 重新加载会话列表并刷新当前页。
    void refreshSessions();
    // 保持会话，重新计数并查询当前页。
    void refreshPage();

signals:
    void logMessage(const QString &text);

private slots:
    void onSessionChanged(int index);
    void onPageSizeChanged(int index);
    void goPrevPage();
    void goNextPage();

private:
    qint64 currentSessionId() const;
    bool queryTotal(qint64 sessionId, qint64 *total, QString *error);
    void updatePageUi();
    void showQueryError(const QString &context, const QSqlError &error);

    QString m_dbPath;
    QSqlDatabase m_readDb; // GUI 线程的连接，必须比 model 活得久
    HistoryQueryModel *m_model = nullptr;

    QComboBox *m_sessionCombo = nullptr;
    QComboBox *m_pageSizeCombo = nullptr;
    QTableView *m_table = nullptr;
    QPushButton *m_prevButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QLabel *m_pageLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;

    qint64 m_totalRows = 0;
    qint64 m_page = 1;      // 1-based
    qint64 m_pageCount = 0; // 0 表示没有数据
    int m_pageSize = 100;
    bool m_open = false;
};