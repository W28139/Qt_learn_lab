#include <QApplication>
#include <QCoreApplication>
#include <QLabel>
#include <QString>

int main(int argc, char *argv[])
{
    // 1. 每个 GUI 程序有且只有一个 QApplication
    QApplication app(argc, argv);

    // 2. 应用元信息（后面代码 show() 的主窗口标题会用到,这里是提前把信息写进去）
    QCoreApplication::setApplicationName(QStringLiteral("Lab00"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    // 3. 创建一个标签作为顶层窗口
    const QString text =
        QStringLiteral("Lab 00 - Hello Qt\n\n"
                       "运行时 Qt 版本: %1\n"
                       "编译时 Qt 版本: %2\n"
                       "C++ 标准值: %3")
            .arg(QString::fromLatin1(qVersion()),   // %1
                 QStringLiteral(QT_VERSION_STR),    // %2
                 QString::number(static_cast<qlonglong>(__cplusplus))); // %3

    QLabel label(text);
    label.setAlignment(Qt::AlignCenter);
    // 设置窗口标题
    label.setWindowTitle(QStringLiteral("%1 %2")
                             .arg(QCoreApplication::applicationName(),
                                  QCoreApplication::applicationVersion()));
    label.resize(460, 240);
    // label.show();

    // 4. 进入事件循环：阻塞在这里，直到最后一个窗口关闭
    return app.exec();
    // return 0;

}