#include <QtTest>

#include "sessionstats.h"

namespace {

TelemetrySample makeSample(int channel, double value, Quality quality = Quality::Good)
{
    TelemetrySample sample;
    sample.channel = static_cast<quint16>(channel);
    sample.value = value;
    sample.quality = quality;
    return sample;
}

} // namespace

class SessionStatsTest final : public QObject
{
    Q_OBJECT

private slots:
    void emptyStats();
    void singleSample();
    void perChannel();
    void qualityCounts();
    void resetClears();
    void mean_data();
    void mean();
};

void SessionStatsTest::emptyStats()
{
    SessionStats stats;
    QCOMPARE(stats.totalCount(), qint64(0));
    QVERIFY(stats.isEmpty());
    QVERIFY(stats.channels().isEmpty());

    ChannelStats out;
    QVERIFY(!stats.channelStats(1, &out));
}

void SessionStatsTest::singleSample()
{
    SessionStats stats;
    stats.addSample(makeSample(3, 42.5));

    QCOMPARE(stats.totalCount(), qint64(1));
    QVERIFY(!stats.isEmpty());
    QCOMPARE(stats.channels(), QList<int>{3});

    ChannelStats out;
    QVERIFY(stats.channelStats(3, &out));
    QCOMPARE(out.count, qint64(1));
    QCOMPARE(out.mean, 42.5);
    QCOMPARE(out.min, 42.5);
    QCOMPARE(out.max, 42.5);
    QCOMPARE(out.good, qint64(1));
}

void SessionStatsTest::perChannel()
{
    SessionStats stats;
    stats.addSample(makeSample(1, 10.0));
    stats.addSample(makeSample(1, 20.0));
    stats.addSample(makeSample(2, 100.0));

    QCOMPARE(stats.totalCount(), qint64(3));
    QCOMPARE(stats.channels(), (QList<int>{1, 2}));

    ChannelStats first;
    QVERIFY(stats.channelStats(1, &first));
    QCOMPARE(first.count, qint64(2));
    QCOMPARE(first.mean, 15.0);
    QCOMPARE(first.min, 10.0);
    QCOMPARE(first.max, 20.0);

    ChannelStats second;
    QVERIFY(stats.channelStats(2, &second));
    QCOMPARE(second.count, qint64(1));
    QCOMPARE(second.mean, 100.0);
}

void SessionStatsTest::qualityCounts()
{
    SessionStats stats;
    stats.addSample(makeSample(7, 1.0, Quality::Good));
    stats.addSample(makeSample(7, 2.0, Quality::Warning));
    stats.addSample(makeSample(7, 3.0, Quality::Bad));
    stats.addSample(makeSample(7, 4.0, Quality::Warning));

    ChannelStats out;
    QVERIFY(stats.channelStats(7, &out));
    QCOMPARE(out.count, qint64(4));
    QCOMPARE(out.good, qint64(1));
    QCOMPARE(out.warning, qint64(2));
    QCOMPARE(out.bad, qint64(1));
    QCOMPARE(out.mean, 2.5);
}

void SessionStatsTest::resetClears()
{
    SessionStats stats;
    stats.addSample(makeSample(1, 5.0));
    stats.addSample(makeSample(2, 6.0));
    stats.reset();

    QVERIFY(stats.isEmpty());
    QCOMPARE(stats.totalCount(), qint64(0));
    QVERIFY(stats.channels().isEmpty());
}

void SessionStatsTest::mean_data()
{
    QTest::addColumn<double>("a");
    QTest::addColumn<double>("b");
    QTest::addColumn<double>("c");
    QTest::addColumn<double>("expected");

    QTest::newRow("1,2,3") << 1.0 << 2.0 << 3.0 << 2.0;
    QTest::newRow("negative") << -5.0 << 0.0 << 5.0 << 0.0;
    QTest::newRow("decimal") << 2.5 << 2.5 << 2.5 << 2.5;
    QTest::newRow("uneven") << 10.0 << 20.0 << 40.0 << (70.0 / 3.0);
}

void SessionStatsTest::mean()
{
    QFETCH(double, a);
    QFETCH(double, b);
    QFETCH(double, c);
    QFETCH(double, expected);

    SessionStats stats;
    stats.addSample(makeSample(1, a));
    stats.addSample(makeSample(1, b));
    stats.addSample(makeSample(1, c));

    ChannelStats out;
    QVERIFY(stats.channelStats(1, &out));
    QVERIFY(qAbs(out.mean - expected) < 1e-12);
}

QTEST_GUILESS_MAIN(SessionStatsTest)

#include "tst_sessionstats.moc"