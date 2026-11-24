#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

namespace {

// 连接参数结构体：用于在“读取表单”和“发起连接”之间传递数据，把 UI 控件和业务逻辑解耦。
struct ConnectionParams
{
    QString host;                 // 主机地址（IP 或主机名）
    int port = 9000;              // 端口号，默认 9000
    QString protocol;             // 协议类型（TCP / UDP）
    QString name;                 // 会话名称
    bool autoReconnect = false;   // 是否开启断线自动重连
};

// 生成当前时间戳字符串（格式：时:分:秒.毫秒），用于日志前缀。
QString nowStamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
}

}

// ConnectionPage：连接配置与状态管理页面
// 负责：
//   * 展示服务器参数表单（主机、端口、协议、会话名、自动重连）；
//   * 提供“连接/断开”按钮；
//   * 维护三态状态机：Disconnected / Connecting / Connected；
//   * 输出会话日志；
//   * 状态变化时通过 stateChanged 信号通知外部（主窗口）。
class ConnectionPage final : public QWidget
{
    Q_OBJECT

public:
    // ---------- 状态枚举 ----------
    enum class State {
        Disconnected,   // 未连接（初始态）
        Connecting,     // 连接中（可被"断开"取消）
        Connected       // 已连接（真实网络握手成功后进入）
    };
    // 把 State 注册到 Qt 元对象系统，让信号槽可以直接传递 State
    Q_ENUM(State)

    explicit ConnectionPage(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        // 搭建表单、按钮、日志等所有子控件
        buildUi();

        // 单次定时器模拟网络握手
        m_connectTimer = new QTimer(this);
        m_connectTimer->setSingleShot(true);
        // 超时后调用 finishConnect()，把状态从 Connecting 推到 Connected
        connect(m_connectTimer, &QTimer::timeout,
                this, &ConnectionPage::finishConnect);

        // 根据初始状态（Disconnected）刷新界面控件启用状态和提示文字
        updateUi();
    }

    State state() const { return m_state; }

    // endpointText()：返回"主机:端口 (协议)"形式的字符串
    // 若尚未连接（m_endpoint 为空），返回 "--"
    QString endpointText() const
    {
        return m_endpoint.isEmpty() ? QStringLiteral("--") : m_endpoint;
    }

public slots:
    // 槽函数：开始连接
    void startConnect()
    {
        // 防重入：只有当状态为 Disconnected 时才允许发起连接
        if (m_state != State::Disconnected)
            return;

        ConnectionParams params;
        QString error;
        // ---- 1. 读取并校验表单参数 ----
        if (!readParams(params, error)) {
            QMessageBox::warning(this, QStringLiteral("参数错误"), error);
            return;
        }

        // ---- 2. 拼接端点字符串 ----
        // 例："127.0.0.1:9000 (TCP)"
        // 存到 m_endpoint，供状态栏、日志、断开提示等复用
        m_endpoint = QStringLiteral("%1:%2 (%3)")
                         .arg(params.host)
                         .arg(params.port)
                         .arg(params.protocol);

        // ---- 3. 状态切到"连接中" ----
        setState(State::Connecting);

        // ---- 4. 写一条连接日志 ----
        appendLog(QStringLiteral("正在连接 %1，会话名称“%2”，自动重连：%3")
                      .arg(m_endpoint, params.name,
                           params.autoReconnect ? QStringLiteral("开")
                                                : QStringLiteral("关")));

        // ---- 5. 启动模拟握手定时器（800ms 后完成）----
        m_connectTimer->start(800);
    }

    // 槽函数：断开/取消当前连接
    void disconnectNow()
    {
        // 已经是未连接状态：什么都不做
        if (m_state == State::Disconnected)
            return;

        // 记录"当时是连接中还是已连接"，用于生成不同的日志文字
        const bool cancelled = (m_state == State::Connecting);

        // 停掉定时器（防止稍后又把状态推到 Connected）
        m_connectTimer->stop();

        // 状态回退到 Disconnected
        setState(State::Disconnected);

        // 根据进入时的状态，记录不同的日志
        //   Connecting → "已取消连接"
        //   Connected  → "已断开连接：host:port (proto)"
        appendLog(cancelled ? QStringLiteral("已取消连接")
                            : QStringLiteral("已断开连接：%1").arg(m_endpoint));
    }

signals:
    // 状态变化信号
    void stateChanged(ConnectionPage::State state);

private slots:
    // 私有槽：模拟握手完成
    // 由 m_connectTimer 的 timeout 信号触发
    void finishConnect()
    {
        // 防御性判断：只有当前是 Connecting 时才继续（防止中途被取消后仍生效）
        if (m_state != State::Connecting)
            return;

        setState(State::Connected);
        appendLog(QStringLiteral("连接成功：%1").arg(m_endpoint));
    }

private:
    // buildUi：搭建页面 UI 与内部信号槽
    // 布局结构：
    //   root (QHBoxLayout)
    //     ├── leftColumn (QVBoxLayout)
    //     │     ├── serverGroup      服务器参数表单
    //     │     ├── operationGroup   提示文字 + 连接/断开按钮
    //     │     └── stretch          顶到顶部
    //     └── logGroup (QGroupBox)   会话日志（拉伸因子 1，占剩余宽度）
    void buildUi()
    {
        // ---------- 服务器参数分组框 ----------
        auto *serverGroup = new QGroupBox(QStringLiteral("服务器"), this);
        // QFormLayout：两列布局（左标签 + 右控件）
        auto *form = new QFormLayout(serverGroup);
        // 设置：所有非固定尺寸的字段都跟随窗口扩展
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        // ---- 主机输入框 ---
        m_hostEdit = new QLineEdit(QStringLiteral("127.0.0.1"), serverGroup);
        m_hostEdit->setPlaceholderText(QStringLiteral("IP 或主机名"));  // 占位提示文字（输入框为空时显示，浅色）
        m_hostEdit->setClearButtonEnabled(true);        // 显示清除按钮（点击一次清空文本），提升可用性

        // ---- 端口 SpinBox ----
        m_portSpin = new QSpinBox(serverGroup);
        m_portSpin->setRange(1, 65535);     // 端口合法范围：1 ~ 65535
        m_portSpin->setValue(9000);         // 默认值

        // ---- 协议下拉框 ----
        m_protocolCombo = new QComboBox(serverGroup);
        m_protocolCombo->addItems(QStringList{QStringLiteral("TCP"),
                                              QStringLiteral("UDP")});

        // ---- 会话名称输入框 ----
        m_nameEdit = new QLineEdit(QStringLiteral("default"), serverGroup);
        // 限制长度 32，避免过长名称影响日志/状态栏展示
        m_nameEdit->setMaxLength(32);

        // ---- 自动重连复选框 ----
        m_autoReconnectCheck = new QCheckBox(QStringLiteral("断线后自动重连"), serverGroup);

        // 逐行添加表单条目
        form->addRow(QStringLiteral("主机(&H)："), m_hostEdit);
        form->addRow(QStringLiteral("端口(&P)："), m_portSpin);
        form->addRow(QStringLiteral("协议："), m_protocolCombo);
        form->addRow(QStringLiteral("会话名称："), m_nameEdit);
        form->addRow(m_autoReconnectCheck);

        // ---------- 操作分组框 ----------
        auto *operationGroup = new QGroupBox(QStringLiteral("操作"), this);

        // 提示标签：会随状态变化显示不同文案（见 updateUi）
        m_hintLabel = new QLabel(operationGroup);
        m_hintLabel->setWordWrap(true);     // setWordWrap：允许换行（避免窗口变窄时文字被截断

        // 设置连接/断开按钮
        m_connectButton = new QPushButton(QStringLiteral("连接"), operationGroup);
        m_disconnectButton = new QPushButton(QStringLiteral("断开"), operationGroup);
        m_disconnectButton->setEnabled(false);

        // 按钮横向排布
        auto *buttonRow = new QHBoxLayout;
        buttonRow->addWidget(m_connectButton);
        buttonRow->addWidget(m_disconnectButton);
        buttonRow->addStretch();     // 弹性空白，把按钮挤到左侧

        // 操作分组纵向布局：提示在上、按钮在下
        auto *opLayout = new QVBoxLayout(operationGroup);
        opLayout->addWidget(m_hintLabel);
        opLayout->addLayout(buttonRow);

        // ---------- 会话日志 ----------
        m_log = new QPlainTextEdit(this);
        m_log->setReadOnly(true);   // 日志区只读
        m_log->setMaximumBlockCount(500);   // 限制段落数
        m_log->setPlaceholderText(QStringLiteral("连接过程与会话日志…"));

        auto *logGroup = new QGroupBox(QStringLiteral("会话日志"), this);
        auto *logLayout = new QVBoxLayout(logGroup);
        logLayout->addWidget(m_log);

        // ---------- 左侧列（表单 + 操作）----------
        auto *leftColumn = new QVBoxLayout;
        leftColumn->addWidget(serverGroup);
        leftColumn->addWidget(operationGroup);
        leftColumn->addStretch();   // 底部弹性空白：让左侧内容"顶到顶部"，避免被纵向拉扯

        // ---------- 根布局：左窄右宽（H 布局）----------
        auto *root = new QHBoxLayout(this);
        root->setContentsMargins(12, 12, 12, 12);  // 四周留白（左,上,右,下）
        root->setSpacing(12);                      // 左右两列之间的间距
        // 左侧列拉伸因子 0：不主动抢宽，保持自然宽度
        root->addLayout(leftColumn, 0);
        // 日志区拉伸因子 1：占据剩余全部宽度（窗口变宽时它变大）
        root->addWidget(logGroup, 1);

        // 点击"连接"按钮 → startConnect()
        connect(m_connectButton, &QPushButton::clicked, this, &ConnectionPage::startConnect);
        // 点击"断开"按钮 → disconnectNow()
        connect(m_disconnectButton, &QPushButton::clicked, this, &ConnectionPage::disconnectNow);
    }

    // 从 UI 控件读取并校验 ConnectionParams
    bool readParams(ConnectionParams &params, QString &error) const
    {
        params.host = m_hostEdit->text().trimmed();
        if (params.host.isEmpty()) {
            error = QStringLiteral("主机地址不能为空。");
            m_hostEdit->setFocus();
            return false;
        }

        params.name = m_nameEdit->text().trimmed();
        if (params.name.isEmpty()) {
            error = QStringLiteral("会话名称不能为空。");
            m_nameEdit->setFocus();
            return false;
        }

        params.port = m_portSpin->value();
        params.protocol = m_protocolCombo->currentText();
        params.autoReconnect = m_autoReconnectCheck->isChecked();
        return true;
    }

    // 统一状态变更入口
    void setState(State state)
    {
        if (m_state == state)
            return;
        m_state = state;
        updateUi();
        emit stateChanged(m_state);
    }

    // 根据当前状态刷新控件启用与提示文字
    void updateUi()
    {
        const bool idle = (m_state == State::Disconnected);
        const bool connected = (m_state == State::Connected);

        // 一次性把所有"参数输入控件"的启用状态设为 idle
        m_hostEdit->setEnabled(idle);
        m_portSpin->setEnabled(idle);
        m_protocolCombo->setEnabled(idle);
        m_nameEdit->setEnabled(idle);
        m_autoReconnectCheck->setEnabled(idle);

        // 按钮状态：连接仅 idle 可用；断开仅在"非 idle"（=连接中/已连接）可用
        m_connectButton->setEnabled(idle);
        m_disconnectButton->setEnabled(!idle);

        // 提示文字随状态切换
        if (idle)
            m_hintLabel->setText(QStringLiteral("填写参数后点击“连接”。"));
        else if (connected)
            m_hintLabel->setText(
                QStringLiteral("已连接，等待数据（单元 06 接入真实网络）。"));
        else
            m_hintLabel->setText(QStringLiteral("正在连接…可点击“断开”取消。"));
    }

    // 追加一行带时间戳的日志
    void appendLog(const QString &text)
    {
        m_log->appendPlainText(QStringLiteral("%1  %2").arg(nowStamp(), text));
    }

    QLineEdit *m_hostEdit = nullptr;             // 主机地址输入
    QSpinBox *m_portSpin = nullptr;              // 端口 SpinBox
    QComboBox *m_protocolCombo = nullptr;        // 协议下拉框（TCP/UDP）
    QLineEdit *m_nameEdit = nullptr;             // 会话名称
    QCheckBox *m_autoReconnectCheck = nullptr;   // 断线自动重连
    QPushButton *m_connectButton = nullptr;      // "连接"按钮
    QPushButton *m_disconnectButton = nullptr;   // "断开/取消"按钮
    QLabel *m_hintLabel = nullptr;               // 状态提示文字
    QPlainTextEdit *m_log = nullptr;             // 会话日志
    QTimer *m_connectTimer = nullptr;            // 模拟握手定时器
    State m_state = State::Disconnected;         // 状态机当前状态
    QString m_endpoint;                          // "host:port (proto)" 端点串
};

// MainWindow：主窗口
// 负责：
//   * 组织菜单栏、工具栏、状态栏、中央标签页；
//   * 把页面状态同步到“连接/断开”动作可用性、状态栏文本；
//   * 关闭窗口时，如果连接未断开需二次确认。
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr)
        : QMainWindow(parent)
    {
        buildUi();
    }

protected:
    // 重写关闭事件：连接未断开时弹窗确认，用户选择“否”则取消关闭
    void closeEvent(QCloseEvent *event) override
    {
        if (m_page->state() != ConnectionPage::State::Disconnected) {
            // 显示“确认”对话框
            const auto answer = QMessageBox::question(
                this,
                QStringLiteral("确认退出"),
                QStringLiteral("当前会话尚未断开，确定要退出吗？"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);   // 默认焦点在“否”，防止误操作
            if (answer != QMessageBox::Yes) {
                event->ignore();    // 取消关闭
                return;
            }
        }
        event->accept();            // 允许关闭
    }

private slots:
    // 响应页面状态变化
    // 1. 更新“连接 / 断开”动作的可用性（菜单和工具栏共用同一个 QAction，所以更新一次即可同步两者）
    // 2. 更新状态栏文本和永久控件（状态标签 + 端点标签）
    void onPageStateChanged(ConnectionPage::State state)
    {
        const bool idle = (state == ConnectionPage::State::Disconnected);
        m_connectAction->setEnabled(idle);
        m_disconnectAction->setEnabled(!idle);

        switch (state) {
        case ConnectionPage::State::Disconnected:
            m_stateLabel->setText(QStringLiteral("未连接"));
            m_endpointLabel->setText(QStringLiteral("--"));
            statusBar()->showMessage(QStringLiteral("已断开"), 3000);
            break;
        case ConnectionPage::State::Connecting:
            m_stateLabel->setText(QStringLiteral("连接中…"));
            m_endpointLabel->setText(m_page->endpointText());
            statusBar()->showMessage(QStringLiteral("正在建立连接…"), 3000);
            break;
        case ConnectionPage::State::Connected:
            m_stateLabel->setText(QStringLiteral("已连接"));
            m_endpointLabel->setText(m_page->endpointText());
            statusBar()->showMessage(QStringLiteral("连接成功"), 3000);
            break;
        }
    }
    // 显示“关于”对话框
    void showAbout()
    {
        QMessageBox::about(
            this,
            QStringLiteral("关于 Lab 03"),
            QStringLiteral("<b>Lab 03 - Client Skeleton</b><br>"
                           "Qt 课程示例：主窗口、布局、表单与状态栏。<br><br>"
                           "Qt 版本：%1")
                .arg(QString::fromLatin1(qVersion())));
    }

private:
    // buildUi()：构建主窗口整体 UI 结构：
    //   QMainWindow
    //     ├─ menuBar()  菜单栏（文件/帮助）
    //     ├─ addToolBar() 工具栏（连接/断开）
    //     ├─ statusBar() 状态栏（左侧消息 + 右侧永久状态标签）
    //     └─ centralWidget = QTabWidget（连接页 + 实时数据占位页）
    void buildUi()
    {
        // ========== 1. 窗口基本属性 ==========
        // 设置窗口标题栏文字（会显示在窗口顶部和任务栏）
        setWindowTitle(QStringLiteral("Lab 03 - Client Skeleton"));
        resize(900, 600);           // resize：设置窗口的初始尺寸（宽 900，高 600）
        setMinimumSize(720, 480);   // setMinimumSize：设置窗口能被缩放到的最小尺寸


        // ========== 2. 创建 QTabWidget 容器 ==========
        // QTabWidget 是一个多页容器，用户通过顶部标签页切换子页面
        auto *tabs = new QTabWidget(this);


        // ========== 3.1 创建连接页 ==========（后面通过信号槽和主窗口联动）
        m_page = new ConnectionPage(this);
        // addTab：向标签页中添加一个页面，第二个参数是标签文字
        tabs->addTab(m_page, QStringLiteral("连接")); // 第 0 页

        // ========== 3.2 创建实时数据(占位)页 ==========
        auto *placeholder = new QLabel(QStringLiteral("实时数据页将在单元 04 接入。"), tabs);
        placeholder->setAlignment(Qt::AlignCenter); // 水平居中 + 垂直居中
        tabs->addTab(placeholder, QStringLiteral("实时数据"));  // 第 1 页

        tabs->setCurrentIndex(0);   // 默认显示“连接”页
        setCentralWidget(tabs);     // 把 QTabWidget 设为 QMainWindow 的中央控件


        // ========== 4. 创建动作（QAction） ==========
        // 连接动作
        auto *connectAction = new QAction(QStringLiteral("连接(&C)"), this);
        connectAction->setShortcut(QKeySequence(QStringLiteral("F5")));   // F5 快捷键
        connectAction->setStatusTip(QStringLiteral("使用当前参数连接服务器")); // 鼠标悬停时状态栏显示的提示文字

        // 断开动作
        auto *disconnectAction = new QAction(QStringLiteral("断开(&D)"), this);
        disconnectAction->setShortcut(QKeySequence(QStringLiteral("Shift+F5")));// Shift+F5 快捷键
        disconnectAction->setStatusTip(QStringLiteral("断开或取消当前连接"));
        disconnectAction->setEnabled(false);    // 断开动作：Shift+F5，初始禁用

        // 退出动作：Ctrl+Q
        auto *quitAction = new QAction(QStringLiteral("退出(&X)"), this);
        quitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
        quitAction->setStatusTip(QStringLiteral("退出程序"));

        // 关于动作
        auto *aboutAction = new QAction(QStringLiteral("关于 Lab 03(&A)"), this);
        aboutAction->setStatusTip(QStringLiteral("显示版本与说明"));

        // 把关键动作存进成员变量，便于状态变化时动态启停
        m_connectAction = connectAction;
        m_disconnectAction = disconnectAction;


        // ========== 5. 菜单栏 ==========
        // menuBar()：首次调用会自动创建菜单栏并附加到 QMainWindow
        // addMenu：创建并返回一个 QMenu*，后续用 addAction 往里加菜单项
        // 文件菜单
        QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
        fileMenu->addAction(connectAction);     // 把"连接"动作加入文件菜单
        fileMenu->addAction(disconnectAction);  // 把"断开"动作加入文件菜单
        fileMenu->addSeparator();               // 在“断开”和“退出”之间插入一条分隔线
        fileMenu->addAction(quitAction);        // 把"退出"动作加入文件菜单

        // 帮助菜单
        QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
        helpMenu->addAction(aboutAction);

        // ========== 6. 工具栏 ==========
        // addToolBar：向主窗口添加工具栏，参数是工具栏的标题
        auto *toolBar = addToolBar(QStringLiteral("主工具栏"));
        // setObjectName：设置对象名，主要用于 QSS 样式表选择器、测试、调试
        toolBar->setObjectName(QStringLiteral("mainToolBar"));
        // setMovable(false)：禁止用户拖拽工具栏到其他位置（固定位置）
        toolBar->setMovable(false);
        // 工具栏按钮的显示样式：只显示文字（不显示图标）
        // 其他可选：IconOnly、TextBesideIcon、TextUnderIcon、ToolButtonIconOnly 等
        toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);

        // 把"连接/断开"动作加入工具栏
        toolBar->addAction(connectAction);
        toolBar->addAction(disconnectAction);


        // ========== 7. 状态栏 ==========
        // 创建两个标签，用于在状态栏右侧显示连接状态和服务器地址
        m_stateLabel = new QLabel(QStringLiteral("未连接"), this);
        m_stateLabel->setMinimumWidth(72);
        m_endpointLabel = new QLabel(QStringLiteral("----------"), this);
        // 将控件添加到状态栏的"永久"区域（右侧）
        statusBar()->addPermanentWidget(m_stateLabel);
        statusBar()->addPermanentWidget(m_endpointLabel);
        // showMessage：在状态栏左侧显示一条临时消息，3000 毫秒（3 秒）后自动消失
        statusBar()->showMessage(QStringLiteral("就绪"), 3000);


        // ========== 8. 信号与槽连接 ==========
        // 点击"连接"动作 → 调用连接页的 startConnect()
        connect(connectAction, &QAction::triggered, m_page, &ConnectionPage::startConnect);

        // 点击"断开"动作 → 调用连接页的 disconnectNow()
        connect(disconnectAction, &QAction::triggered, m_page, &ConnectionPage::disconnectNow);

        // 点击"退出"动作 → 关闭主窗口
        connect(quitAction, &QAction::triggered, this, &QWidget::close);

        // 点击"关于"动作 → 调用 MainWindow 的 showAbout() 槽
        connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);

        // 连接页状态变化 → 主窗口的 onPageStateChanged() 槽
        // 用法：在状态变化时更新状态栏文字、启用/禁用"连接/断开"动作
        connect(m_page, &ConnectionPage::stateChanged, this, &MainWindow::onPageStateChanged);
    }

    ConnectionPage *m_page = nullptr;            // 指向"连接"页（信号槽联动、状态查询）

    QAction *m_connectAction = nullptr;         // 指向"连接"动作（用于动态启停）
    QAction *m_disconnectAction = nullptr;      // 指向"断开"动作（用于动态启停）

    QLabel *m_stateLabel = nullptr;             // 状态栏"连接状态"标签
    QLabel *m_endpointLabel = nullptr;          // 状态栏标签 "----"
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setApplicationName(QStringLiteral("Lab03"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    MainWindow window;
    window.show();
    return app.exec();
}

// 类定义在 main.cpp 里且使用了 Q_OBJECT，必须包含 moc 生成的代码
#include "main.moc"