#include <QtTest>

#include "csvutils.h"

class CsvUtilsTest final : public QObject
{
    Q_OBJECT

private slots:
    void escapeField_data();
    void escapeField();
    void line();
};

void CsvUtilsTest::escapeField_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("plain") << QStringLiteral("abc") << QStringLiteral("abc");
    QTest::newRow("empty") << QString() << QString();
    QTest::newRow("comma") << QStringLiteral("a,b") << QStringLiteral("\"a,b\"");
    QTest::newRow("quote") << QStringLiteral("He said \"hi\"")
                           << QStringLiteral("\"He said \"\"hi\"\"\"");
    QTest::newRow("lf") << QStringLiteral("a\nb") << QStringLiteral("\"a\nb\"");
    QTest::newRow("crlf") << QStringLiteral("a\r\nb") << QStringLiteral("\"a\r\nb\"");
    QTest::newRow("chinese") << QStringLiteral("正常") << QStringLiteral("正常");
}

void CsvUtilsTest::escapeField()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);

    QCOMPARE(csv::escapeField(input), expected);
}

void CsvUtilsTest::line()
{
    const QByteArray line = csv::toUtf8Line(
        {QStringLiteral("a,b"), QStringLiteral("c\"d"), QStringLiteral("正常")});

    QCOMPARE(line, QStringLiteral("\"a,b\",\"c\"\"d\",正常\r\n").toUtf8());
}

QTEST_GUILESS_MAIN(CsvUtilsTest)

#include "tst_csvutils.moc"