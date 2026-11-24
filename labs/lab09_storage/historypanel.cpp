#include "historypanel.h"

#include "databaseschema.h"
#include "telemetrytypes.h"

#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableView>
#include <QVBoxLayout>

#include <utility>

namespace {

const char kConnectionName[] = "lab09_reader";
constexpr int kMaxPageSize = 500;

} // namespace

QVariant HistoryQueryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0: { // ts_ms
            const qint64 ms = QSqlQueryModel::data(index, Qt::DisplayRole).toLongLong();
            return QDateTime::fromMSecsSinceEpoch(ms)
                .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        }
        case 1: // channel
            return QStringLiteral("CH %1")
                .arg(QSqlQueryModel::data(index, Qt::DisplayRole).toInt());
        case 2: // value
            return QString::number(
                QSqlQueryModel::data(index, Qt::DisplayRole).toDouble(), 'f', 2);
        case 3: { // quality
            const int value = QSqlQueryModel::data(index, Qt::DisplayRole).toInt();
            return qualityText(static_cast<Quality>(value));
        }
        default:
            break;
        }
    } else if (role == Qt::TextAlignmentRole) {
        if (index.column() == 2)
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        return static_cast<int>(Qt::AlignCenter);
    }

    return QSqlQueryModel::data(index, role);
}

HistoryPanel::HistoryPanel(QString dbPath, QWidget *parent)
    : QWidget(parent)
    , m_dbPath(std::move(dbPath))
{
    m_sessionCombo = new QComboBox(this);
    m_sessionCombo->setMinimumWidth(360);
    m_pageSizeCombo = new QComboBox(this);
    m_pageSizeCombo->addItem(QStringLiteral("50 行"), 50);
    m_pageSizeCombo->addItem(QStringLiteral("100 行"), 100);
    m_pageSizeCombo->addItem(QStringLiteral("200 行"), 200);
    m_pageSizeCombo->addItem(QStringLiteral("500 行"), kMaxPageSize);
    m_pageSizeCombo->setCurrentIndex(1);

    m_prevButton = new QPushButton(QStringLiteral("上一页"), this);
    m_nextButton = new QPushButton(QStringLiteral("下一页"), this);
    auto *refreshButton = new QPushButton(QStringLiteral("刷新"), this);
    m_pageLabel = new QLabel(QStringLiteral("第 0 / 0 页"), this);
    m_summaryLabel = new QLabel(QStringLiteral("数据库尚未就绪"), this);
    m_summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_model = new HistoryQueryModel(this);
    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(0, 180); // 时间
    m_table->setColumnWidth(1, 70);  // 通道
    m_table->setColumnWidth(2, 100); // 数值

    auto *top = new QHBoxLayout;
    top->setSpacing(8);
    top->addWidget(new QLabel(QStringLiteral("会话："), this));
    top->addWidget(m_sessionCombo, 1);
    top->addWidget(refreshButton);
    top->addSpacing(12);
    top->addWidget(new QLabel(QStringLiteral("每页："), this));
    top->addWidget(m_pageSizeCombo);
    top->addSpacing(12);
    top->addWidget(m_prevButton);
    top->addWidget(m_pageLabel);
    top->addWidget(m_nextButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addLayout(top);
    layout->addWidget(m_table, 1);
    layout->addWidget(m_summaryLabel);

    connect(refreshButton, &QPushButton::clicked, this, &HistoryPanel::refreshSessions);
    connect(m_prevButton, &QPushButton::clicked, this, &HistoryPanel::goPrevPage);
    connect(m_nextButton, &QPushButton::clicked, this, &HistoryPanel::goNextPage);
    connect(m_sessionCombo, &QComboBox::currentIndexChanged,
            this, &HistoryPanel::onSessionChanged);
    connect(m_pageSizeCombo, &QComboBox::currentIndexChanged,
            this, &HistoryPanel::onPageSizeChanged);

    updatePageUi();
}

HistoryPanel::~HistoryPanel()
{
    // 顺序：先断开视图与模型、销毁模型（内部有 QSqlQuery），
    // 再关闭连接并移除连接名。否则会出现
    // "connection 'lab09_reader' is still in use" 警告。
    if (m_table)
        m_table->setModel(nullptr);
    delete m_model;
    m_model = nullptr;

    if (m_readDb.isOpen())
        m_readDb.close();
    m_readDb = QSqlDatabase();
    if (QSqlDatabase::contains(QString::fromLatin1(kConnectionName)))
        QSqlDatabase::removeDatabase(QString::fromLatin1(kConnectionName));
}

bool HistoryPanel::open(QString *error)
{
    if (m_open)
        return true;

    if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        if (error)
            *error = QStringLiteral("SQLite driver (QSQLITE) is not available");
        return false;
    }

    // 只读查询也要自己的连接：它属于 GUI 线程，不能借用 writer 线程的连接。
    const QString connectionName = QString::fromLatin1(kConnectionName);
    if (QSqlDatabase::contains(connectionName))
        QSqlDatabase::removeDatabase(connectionName);

    m_readDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    m_readDb.setDatabaseName(m_dbPath);
    if (!m_readDb.open()) {
        if (error)
            *error = QStringLiteral("cannot open read connection: %1")
                         .arg(db::describeError(m_readDb.lastError()));
        m_readDb = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
        return false;
    }

    QString configError;
    if (!db::configureReader(m_readDb, &configError)) {
        if (error)
            *error = configError;
        m_readDb.close();
        m_readDb = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
        return false;
    }

    m_open = true;
    m_table->setModel(m_model);
    refreshSessions();
    emit logMessage(QStringLiteral("history reader opened: %1").arg(m_dbPath));
    return true;
}

void HistoryPanel::refreshSessions()
{
    if (!m_open)
        return;

    const qint64 previousId = currentSessionId();

    {
        const QSignalBlocker blocker(m_sessionCombo);
        m_sessionCombo->clear();

        QSqlQuery query(m_readDb);
        query.setForwardOnly(true);
        if (!query.exec(QStringLiteral(
                "SELECT id, started_ms, ended_ms, sample_count"
                " FROM sessions ORDER BY id DESC"))) {
            showQueryError(QStringLiteral("load sessions"), query.lastError());
            m_model->clear();
            m_summaryLabel->setText(QStringLiteral("读取会话失败"));
            updatePageUi();
            return;
        }

        while (query.next()) {
            const qint64 id = query.value(0).toLongLong();
            const qint64 startedMs = query.value(1).toLongLong();
            const bool running = query.value(2).isNull();
            const qint64 sampleCount = query.value(3).toLongLong();

            QString text = QStringLiteral("#%1 · %2")
                               .arg(id)
                               .arg(QDateTime::fromMSecsSinceEpoch(startedMs)
                                        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
            if (running)
                text += QStringLiteral(" · 采集中");
            else
                text += QStringLiteral(" · %1 行").arg(sampleCount);

            m_sessionCombo->addItem(text, QVariant::fromValue(id));
        }

        int index = 0;
        if (previousId > 0) {
            const int found = m_sessionCombo->findData(QVariant::fromValue(previousId));
            if (found >= 0)
                index = found;
        }
        if (m_sessionCombo->count() > 0)
            m_sessionCombo->setCurrentIndex(index);
    }

    if (m_sessionCombo->count() == 0) {
        m_model->clear();
        m_totalRows = 0;
        m_pageCount = 0;
        m_page = 1;
        m_summaryLabel->setText(QStringLiteral("还没有采集记录"));
        updatePageUi();
        return;
    }

    refreshPage();
}

void HistoryPanel::refreshPage()
{
    if (!m_open)
        return;

    const qint64 sessionId = currentSessionId();
    if (sessionId <= 0) {
        m_model->clear();
        m_totalRows = 0;
        m_pageCount = 0;
        m_page = 1;
        updatePageUi();
        return;
    }

    qint64 total = 0;
    QString countError;
    if (!queryTotal(sessionId, &total, &countError)) {
        m_model->clear();
        m_summaryLabel->setText(countError);
        updatePageUi();
        return;
    }

    m_totalRows = total;
    m_pageCount = (total + m_pageSize - 1) / m_pageSize;
    if (m_pageCount <= 0) {
        m_model->clear();
        m_page = 1;
        m_pageCount = 0;
        m_summaryLabel->setText(QStringLiteral("会话 #%1 · 0 行").arg(sessionId));
        updatePageUi();
        return;
    }

    if (m_page < 1)
        m_page = 1;
    if (m_page > m_pageCount)
        m_page = m_pageCount;

    const qint64 offset = (m_page - 1) * m_pageSize;

    // LIMIT/OFFSET 分页：页内按 (ts_ms, id) 稳定排序。
    // id 作为第二排序键，保证同一毫秒的多个样本也不会在页面间漂移。
    QSqlQuery query(m_readDb);
    query.prepare(QStringLiteral(
        "SELECT ts_ms, channel, value, quality"
        " FROM samples WHERE session_id = ?"
        " ORDER BY ts_ms, id LIMIT ? OFFSET ?"));
    query.addBindValue(sessionId);
    query.addBindValue(m_pageSize);
    query.addBindValue(offset);
    if (!query.exec()) {
        showQueryError(QStringLiteral("load page %1").arg(m_page), query.lastError());
        m_model->clear();
        m_summaryLabel->setText(QStringLiteral("查询失败，详见控制台"));
        updatePageUi();
        return;
    }

    // Qt 6.4 起提供 setQuery(QSqlQuery&&)：把已执行的查询移动给模型，
    // 避免复制 QSqlQuery（内部持有驱动结果集）。
    m_model->setQuery(std::move(query));
    m_model->setHeaderData(0, Qt::Horizontal, QStringLiteral("时间"));
    m_model->setHeaderData(1, Qt::Horizontal, QStringLiteral("通道"));
    m_model->setHeaderData(2, Qt::Horizontal, QStringLiteral("数值"));
    m_model->setHeaderData(3, Qt::Horizontal, QStringLiteral("质量"));

    // setQuery() 之后列才存在，列宽也要在这时设置（否则会被模型重置回默认值）。
    m_table->setColumnWidth(0, 190);
    m_table->setColumnWidth(1, 75);
    m_table->setColumnWidth(2, 100);

    m_summaryLabel->setText(QStringLiteral("会话 #%1 · 共 %2 行 · 每页 %3 行 · 本页从第 %4 行开始")
                                .arg(sessionId)
                                .arg(m_totalRows)
                                .arg(m_pageSize)
                                .arg(offset + 1));
    updatePageUi();
}

void HistoryPanel::onSessionChanged(int index)
{
    Q_UNUSED(index);
    m_page = 1;
    refreshPage();
}

void HistoryPanel::onPageSizeChanged(int index)
{
    Q_UNUSED(index);
    m_pageSize = m_pageSizeCombo->currentData().toInt();
    m_page = 1;
    refreshPage();
}

void HistoryPanel::goPrevPage()
{
    if (m_page <= 1)
        return;
    --m_page;
    refreshPage();
}

void HistoryPanel::goNextPage()
{
    if (m_page >= m_pageCount)
        return;
    ++m_page;
    refreshPage();
}

qint64 HistoryPanel::currentSessionId() const
{
    if (!m_sessionCombo || m_sessionCombo->currentIndex() < 0)
        return 0;
    return m_sessionCombo->currentData().toLongLong();
}

bool HistoryPanel::queryTotal(qint64 sessionId, qint64 *total, QString *error)
{
    QSqlQuery query(m_readDb);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM samples WHERE session_id = ?"));
    query.addBindValue(sessionId);
    if (!query.exec() || !query.next()) {
        if (error)
            *error = QStringLiteral("COUNT failed: %1")
                         .arg(db::describeError(query.lastError()));
        return false;
    }
    if (total)
        *total = query.value(0).toLongLong();
    return true;
}

void HistoryPanel::updatePageUi()
{
    const bool hasSession = m_sessionCombo && m_sessionCombo->count() > 0;
    m_pageLabel->setText(m_pageCount > 0
                             ? QStringLiteral("第 %1 / %2 页").arg(m_page).arg(m_pageCount)
                             : QStringLiteral("第 0 / 0 页"));
    m_prevButton->setEnabled(hasSession && m_pageCount > 0 && m_page > 1);
    m_nextButton->setEnabled(hasSession && m_pageCount > 0 && m_page < m_pageCount);
    m_table->setEnabled(hasSession);
    m_sessionCombo->setEnabled(hasSession);
}

void HistoryPanel::showQueryError(const QString &context, const QSqlError &error)
{
    const QString text = QStringLiteral("%1: %2").arg(context, db::describeError(error));
    qWarning().noquote() << "[history]" << text;
    emit logMessage(text);
}