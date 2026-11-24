#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QThread>

// final 关键字表示这个类不允许被其他类继承
class TimerWindow final : public QWidget
{
    Q_OBJECT    // 必须加这个宏，用于启用 Qt 的信号槽机制和元对象系统 (MOC)

public:
    // explicit 防止隐式类型转换；parent 默认为 nullptr，代表独立窗口
    explicit TimerWindow(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        // 设置窗口标题
        setWindowTitle(QStringLiteral("Lab 01 - Timer"));

        // ---------------- 1. 初始化 UI 控件 ----------------
        // this 作为父对象，由 Qt 自动管理内存
        m_clockLabel = new QLabel(this);
        // 设置文本居中对齐
        m_clockLabel->setAlignment(Qt::AlignCenter);
        // 获取当前默认字体,并修改字体大小为 26，然后应用新字体
        QFont clockFont = m_clockLabel->font();
        clockFont.setPointSize(26);
        m_clockLabel->setFont(clockFont);

        m_elapsedLabel = new QLabel(this);
        m_elapsedLabel->setAlignment(Qt::AlignCenter);

        m_statusLabel = new QLabel(QStringLiteral("运行中"), this);
        m_statusLabel->setAlignment(Qt::AlignCenter);

        m_toggleButton = new QPushButton(QStringLiteral("暂停"), this);
        // 非成员变量（因为这几个字不会被修改，不需要设置为成员变量）
        auto *remindButton = new QPushButton(QStringLiteral("2 秒后提醒"), this);

        auto *blockButton = new QPushButton(QStringLiteral("阻塞主线程 3 秒(会卡死)"), this);
        auto *nonBlockButton = new QPushButton(QStringLiteral("非阻塞 3 秒(不卡死)"), this);

        // ---------------- 2. 布局管理 ----------------
        // 垂直布局，并直接设置给当前窗口 this
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(m_clockLabel);
        layout->addWidget(m_elapsedLabel);
        layout->addWidget(m_statusLabel);
        layout->addWidget(m_toggleButton);
        layout->addWidget(remindButton);

        layout->addWidget(blockButton);
        layout->addWidget(nonBlockButton);

        // ---------------- 3. 定时器与信号槽配置 --------------（触发不同信号时，执行不同函数）
        // 创建定时器。注意：传入 this 作为父对象，当窗口销毁时定时器会自动停止并释放内存
        m_clockTimer = new QTimer(this);
        // 设置触发间隔为 100 毫秒 (0.1秒)
        m_clockTimer->setInterval(100);
        // 连接定时器的 timeout 信号到自定义的槽函数 refreshClock
        connect(m_clockTimer, &QTimer::timeout,
                this, &TimerWindow::refreshClock);

        // 连接按钮的 clicked 信号到自定义的槽函数 toggleClock/scheduleReminder
        connect(m_toggleButton, &QPushButton::clicked,
                this, &TimerWindow::toggleClock);
        connect(remindButton, &QPushButton::clicked,
                this, &TimerWindow::scheduleReminder);


        // 真阻塞：点击后界面会卡死 3 秒
        connect(blockButton, &QPushButton::clicked, this, [this] {
            m_statusLabel->setText(QStringLiteral("开始真阻塞，界面即将卡死..."));

            // 强制刷新一次界面，让你能在卡死前看到这行字
            QCoreApplication::processEvents();

            QThread::sleep(3); // 阻塞主线程 3 秒！

            m_statusLabel->setText(QStringLiteral("真阻塞结束，界面恢复"));
        });

        // 非阻塞：延迟 3 秒执行，但界面依然可以点
        connect(nonBlockButton, &QPushButton::clicked, this, [this] {
            m_statusLabel->setText(QStringLiteral("非阻塞等待 3 秒，你可以点我旁边的按钮..."));

            // 3 秒后才执行 lambda 里的代码，这段时间主线程是空闲的
            QTimer::singleShot(3000, this, [this] {
                m_statusLabel->setText(QStringLiteral("非阻塞 3 秒到！"));
            });
        });

        // 当 QApplication 即将退出时，打印一条日志 (使用 lambda 表达式)
        connect(qApp, &QCoreApplication::aboutToQuit, this, [] {
            qInfo() << "aboutToQuit: 正常退出";
        });


        // ---------------- 4. 启动 ----------------
        m_elapsed.start();  // 启动高精度计时器
        m_clockTimer->start();  // 启动定时器，开始周期性触发
        refreshClock();         // 手动调用一次，防止界面在最初的 100ms 内空白

        resize(460, 300);       // 设置窗口初始大小
    }

private slots:  // 槽函数声明区，这里可以接收信号
    // 刷新时钟的槽函数
    void refreshClock()
    {
        // QDateTime::toString 格式化字符串
        m_clockLabel->setText(
            QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")));
        // m_elapsed.elapsed() 返回从 start() 开始经过的毫秒数
        m_elapsedLabel->setText(
            QStringLiteral("自启动以来：%1 ms").arg(m_elapsed.elapsed()));
    }

    // 暂停/继续 按钮的槽函数
    void toggleClock()
    {
        // 如果定时器正在运行
        if (m_clockTimer->isActive())
        {
            // 停止定时器（时间停止刷新）
            m_clockTimer->stop();
            m_toggleButton->setText(QStringLiteral("继续"));
             // 证明 UI 线程没有被阻塞
            m_statusLabel->setText(QStringLiteral("已暂停：界面仍可响应"));
        }
         // 如果定时器已停止
        else
        {
            // 重新启动定时器
            m_clockTimer->start();
            m_toggleButton->setText(QStringLiteral("暂停"));
            m_statusLabel->setText(QStringLiteral("运行中"));
        }
    }
    // 提醒按钮的槽函数
    void scheduleReminder()
    {
        m_statusLabel->setText(QStringLiteral("2 秒后提醒…"));
        QTimer::singleShot(2000, this, [this] {
            m_statusLabel->setText(QStringLiteral("提醒：2 秒到！"));
            // 嵌套一个 singleShot，再过 2 秒后把文字改为“就绪”
            QTimer::singleShot(2000, this, [this] {
                m_statusLabel->setText(QStringLiteral("就绪"));
            });
        });
    }

private:
    // 指针成员变量，使用 m_ 前缀是 C++ 的命名惯例，表示 member
    QLabel *m_clockLabel = nullptr;
    QLabel *m_elapsedLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_toggleButton = nullptr;
    QTimer *m_clockTimer = nullptr;
    QElapsedTimer m_elapsed;    // 非指针对象，在窗口构造时自动创建，析构时自动销毁
    /* m_elapsed 不用指针原因：
    QElapsedTimer：它不继承自 QObject，只是一个纯粹的 C++ 工具类，所以它无法享受 Qt 的父子对象自动销毁机制，如果写在堆空间，必须在析构函数里手动 delete m_elapsed;
    所以：
        凡是继承自 QObject 的类（如 QWidget, QPushButton, QTimer, QNetworkAccessManager 等）
            通常使用指针（new 出来），并尽可能传入 parent 指针（或加入布局），让 Qt 自动帮你管理内存。

        凡是纯数据类或工具类（如 QString, QDateTime, QElapsedTimer, QPoint, QColor, std::vector 等）
            直接使用非指针对象（值类型）。它们内部做了良好的优化（如 QString 的隐式共享），直接当普通变量用最方便   。
    */
};

// =========================================================================
// 程序入口
// =========================================================================

int main(int argc, char *argv[])
{
    // 创建 QApplication 对象，管理应用程序级别的资源（如事件循环、剪贴板、字体等）
    QApplication app(argc, argv);

    // 设置应用程序的全局元数据（操作系统任务管理器里能看到）
    QCoreApplication::setApplicationName(QStringLiteral("Lab01"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    TimerWindow window; // 在栈上创建窗口对象
    window.show();      // 显示窗口

    // app.exec() 进入主事件循环。程序会在这里阻塞，直到窗口关闭。
    // 定时器的触发、按钮点击等事件，都是在这个循环中被分发给对应槽函数的。
    return app.exec();
}

// 因为 TimerWindow 类直接定义在 main.cpp 中且包含了 Q_OBJECT 宏，
// 所以必须在这里手动包含 MOC (元对象编译器) 自动生成的代码，否则会报链接错误。
// 注意：这行代码必须放在文件的最后一行。
#include "main.moc"