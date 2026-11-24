#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {

// 返回当前线程的标识字符串（"0x" + 十六进制地址）
QString threadTag()
{
    return QStringLiteral("0x")
    + QString::number(
        reinterpret_cast<quintptr>(QThread::currentThread()), 16);
}

} // namespace

class Worker final : public QObject
{
    Q_OBJECT

public:
    explicit Worker(QObject *parent = nullptr) : QObject(parent) {}

public slots:    // 槽函数区
    // 启动：执行 count 次、间隔 intervalMs 毫秒
    void start(int count, int intervalMs)
    {
        // 懒创建：保证 QTimer 创建并启动在 worker 线程, 首次调用时才创建定时器
        if (!m_timer) {
            m_timer = new QTimer(this); // 父对象设为 this（Worker），随 Worker 销毁
             // 定时器超时 → produceTick 槽（同线程，Direct）
            connect(m_timer, &QTimer::timeout, this, &Worker::produceTick);
        }

        m_timer->stop();    // 若之前在运行，先停止
        m_value = 0;        // 计数器归零
        m_count = count;    // 记录总次数
        m_timer->setInterval(intervalMs);   // 设置触发间隔
        m_timer->start();   // 启动定时器

        // 发出日志消息信号
        emit message(QStringLiteral("Worker::start 在 %1 执行")
                         .arg(threadTag()));
    }

    // 停止定时器
    void stop()
    {
        if (m_timer && m_timer->isActive()) {
            m_timer->stop();
            emit message(QStringLiteral("Worker::stop 在 %1 执行，停在 %2")
                             .arg(threadTag())
                             .arg(m_value));
        }
    }

signals:    // 信号声明区
    void tick(int value);               // 每次 tick 携带当前计数值
    void finished(int total);           // 全部完成时携带总次数
    void message(const QString &text);  // 文本日志消息

private slots:  // 私有槽函数区
    // 定时器超时回调：产生一次 tick
    void produceTick()
    {
        ++m_value;           // 计数加一
        emit tick(m_value);  // 发出 tick 信号（接收者线程由连接类型决定）

        if (m_value >= m_count) {
            m_timer->stop();
            // 法出 message 信号（接收者线程由连接类型决定）
            emit message(QStringLiteral("Worker 完成：共 %1 次").arg(m_value));
            emit finished(m_value);
        }
    }

private:
    QTimer *m_timer = nullptr;
    int m_value = 0;
    int m_count = 0;
};

class SignalWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit SignalWindow(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setWindowTitle(QStringLiteral("Lab 02 - Signals & Threads"));
        resize(560, 420);

        // 创建显示 UI 线程标识的标签
        m_uiThreadLabel = new QLabel(QStringLiteral("UI 线程：") + threadTag(), this);

        // 创建显示 tick 值的标签
        m_countLabel = new QLabel(QStringLiteral("tick: 0"), this);
        // 修改 m_countLabel 标签的字体
        QFont countFont = m_countLabel->font();
        countFont.setPointSize(22);
        m_countLabel->setFont(countFont);

        // 创建显示状态标签（就绪/完成）
        m_statusLabel = new QLabel(QStringLiteral("就绪"), this);

        // 设置 3个 按钮
        auto *startButton = new QPushButton(QStringLiteral("开始（100 次 / 50ms）"), this);
        auto *stopButton  = new QPushButton(QStringLiteral("停止"), this);
        auto *clearButton = new QPushButton(QStringLiteral("清空日志"), this);

        // 设置日志显示控件
        m_log = new QPlainTextEdit(this);   // 获取组件
        m_log->setReadOnly(true);           // 设为只读
        m_log->setMaximumBlockCount(300);   // 最多保留 300 行，超出自动丢弃最旧

        // 按钮行：水平布局
        auto *buttonRow = new QHBoxLayout;
        buttonRow->addWidget(startButton);  // 添加开始按钮
        buttonRow->addWidget(stopButton);   // 添加停止按钮
        buttonRow->addWidget(clearButton);  // 添加清空按钮

        // 主布局：垂直
        auto *root = new QVBoxLayout(this);
        root->addWidget(m_uiThreadLabel);   // 添加 UI 线程标签
        root->addWidget(m_countLabel);      // 添加 tick 值标签
        root->addWidget(m_statusLabel);     // 添加状态标签
        root->addLayout(buttonRow);         // 添加按钮行
        root->addWidget(m_log);             // 添加日志控件


        // ---------- worker 线程 ----------
        m_workerThread = new QThread(this);         // 创建 QThread，父对象为窗口（随窗口销毁）
        m_worker = new Worker;                      // 创建 Worker，不能有父对象，才能 moveToThread
        m_worker->moveToThread(m_workerThread);     //  将 Worker 的所有权移到新线程

        // 线程结束时让 Worker 在其所属线程 deleteLater（安全释放）
        connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

        m_workerThread->start();               // 默认 run() 会启动事件循环

        // UI → worker：AutoConnection，接收者在 worker 线程 → 排队
        // 点击开始 → 在 worker 线程执行 lambda
        connect(startButton, &QPushButton::clicked, m_worker, [this] {
            m_worker->start(100, 50);
        });
        // 点击停止 → 在 worker 线程执行 lambda
        connect(stopButton, &QPushButton::clicked, m_worker, [this] {
            m_worker->stop();
        });
        // 点击清空 → 在 UI 线程执行 lambda
        connect(clearButton, &QPushButton::clicked, this, [this] {
            m_log->clear();
        });

        // worker → UI：显式 QueuedConnection，槽在 UI 线程执行
        connect(m_worker, &Worker::tick,    // tick 信号
                this, &SignalWindow::onTickQueued, Qt::QueuedConnection);

        // 反例演示：DirectConnection，槽在 worker 线程执行
        connect(m_worker, &Worker::tick,    // tick 信号
                this, &SignalWindow::onTickDirect, Qt::DirectConnection);

        connect(m_worker, &Worker::message, // message 信号
                this, &SignalWindow::onWorkerMessage);

         // finished 信号 → lambda 在 UI 线程执行
        connect(m_worker, &Worker::finished, this, [this](int total) {
            m_statusLabel->setText(QStringLiteral("完成：共 %1 次").arg(total)); // 更新状态标签
            appendLog(QStringLiteral("finished 信号收到，total=%1").arg(total)); // 追加日志
        });
    }

    ~SignalWindow() override
    {
        m_workerThread->quit();     // 请求退出 worker 线程事件循环
        m_workerThread->wait();     // 阻塞等待线程真正结束（确保 deleteLater 已处理）
    }

private slots:
    // 排队连接槽：在 UI 线程执行
    void onTickQueued(int value)
    {
        // 安全更新 UI 标签（当前次数）
        m_countLabel->setText(QStringLiteral("tick: %1").arg(value));
        // 每 10 次在打一条日志
        if (value % 10 == 0) {
            appendLog(QStringLiteral("queued 槽：value=%1，线程=%2")
                          .arg(value)
                          .arg(threadTag()));
        }
    }

    // 仅把日志输出到控制台
    void onTickDirect(int value)
    {
        // 这个槽运行在 worker 线程，只写日志，绝不碰任何 QWidget！
        qInfo() << "[DirectConnection] value =" << value
                << "thread =" << QThread::currentThread();
    }

    void onWorkerMessage(const QString &text)
    {
        appendLog(QStringLiteral("[worker] %1").arg(text));
    }

private:
    // 追加一条带时间戳的日志
    void appendLog(const QString &text)
    {
        const QString stamp = QDateTime::currentDateTime()
        .toString(QStringLiteral("HH:mm:ss.zzz"));
        m_log->appendPlainText(QStringLiteral("%1  %2").arg(stamp, text));
    }

    QLabel *m_uiThreadLabel = nullptr;
    QLabel *m_countLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QThread *m_workerThread = nullptr;
    Worker *m_worker = nullptr;
};

int main(int argc, char *argv[])
{
    // 创建 GUI 应用对象（内部创建事件循环）
    QApplication app(argc, argv);

    QCoreApplication::setApplicationName(QStringLiteral("Lab02"));  // 设置应用名称
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0")); // 设置应用版本

    SignalWindow window;    // 创建主窗口
    window.show();          // 显示窗口
    return app.exec();      // 进入事件循环，直到退出
}

// 类定义在 main.cpp 里且使用了 Q_OBJECT，必须包含 moc 生成的代码
#include "main.moc"