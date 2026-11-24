#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("QtLearn"));
    QCoreApplication::setApplicationName(QStringLiteral("lab11_release"));
#ifdef LAB11_VERSION
    QCoreApplication::setApplicationVersion(QString::fromLatin1(LAB11_VERSION));
#else
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
#endif

    MainWindow window;
    window.show();
    return app.exec();
}