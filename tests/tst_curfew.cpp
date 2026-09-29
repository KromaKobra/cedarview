// The curfew rule, at its edges.
//
// Every interesting case is around midnight: Friday and Saturday nights end the
// next morning, the other five end before midnight, and the answer has to move
// on to the next night the moment curfew passes.

#include "testsupport.h"

#include "core/curfew.h"

using namespace mycu;

class TestCurfew : public QObject
{
    Q_OBJECT

private slots:
    // 2026-09-27 is a Sunday.
    void eachNightsCurfew_data()
    {
        QTest::addColumn<QDate>("night");
        QTest::addColumn<QDateTime>("expected");
        QTest::newRow("sunday") << QDate(2026, 9, 27) << QDateTime(QDate(2026, 9, 27), QTime(23, 59));
        QTest::newRow("thursday") << QDate(2026, 10, 1) << QDateTime(QDate(2026, 10, 1), QTime(23, 59));
        QTest::newRow("friday") << QDate(2026, 10, 2) << QDateTime(QDate(2026, 10, 3), QTime(0, 59));
        QTest::newRow("saturday") << QDate(2026, 10, 3) << QDateTime(QDate(2026, 10, 4), QTime(0, 59));
    }
    void eachNightsCurfew()
    {
        QFETCH(QDate, night);
        QFETCH(QDateTime, expected);
        QCOMPARE(curfewFor(night), expected);
    }

    void whichNightIsNext_data()
    {
        QTest::addColumn<QDateTime>("now");
        QTest::addColumn<QDate>("expected");
        const QDate tue(2026, 9, 29), wed(2026, 9, 30);
        const QDate fri(2026, 10, 2), sat(2026, 10, 3), sun(2026, 10, 4), mon(2026, 10, 5);

        QTest::newRow("weekday afternoon") << QDateTime(tue, QTime(15, 0)) << tue;
        QTest::newRow("a minute before") << QDateTime(tue, QTime(23, 58, 59)) << tue;
        QTest::newRow("weekday curfew passes") << QDateTime(tue, QTime(23, 59)) << wed;
        QTest::newRow("just after midnight on a weekday") << QDateTime(wed, QTime(0, 30)) << wed;

        // Friday night runs into Saturday morning.
        QTest::newRow("friday evening") << QDateTime(fri, QTime(23, 30)) << fri;
        QTest::newRow("still friday night") << QDateTime(sat, QTime(0, 58)) << fri;
        QTest::newRow("friday curfew passes") << QDateTime(sat, QTime(0, 59)) << sat;
        QTest::newRow("saturday morning") << QDateTime(sat, QTime(9, 0)) << sat;
        QTest::newRow("still saturday night") << QDateTime(sun, QTime(0, 30)) << sat;

        // Saturday night's late curfew must not leak into Sunday night's.
        QTest::newRow("sunday after saturday curfew") << QDateTime(sun, QTime(1, 0)) << sun;
        QTest::newRow("sunday curfew passes") << QDateTime(sun, QTime(23, 59)) << mon;
        QTest::newRow("monday just after midnight") << QDateTime(mon, QTime(0, 30)) << mon;
    }
    void whichNightIsNext()
    {
        QFETCH(QDateTime, now);
        QFETCH(QDate, expected);
        QCOMPARE(curfewNight(now), expected);
    }

    // Whatever the moment, the curfew reported is still ahead of it, and by no
    // more than a day and an hour.
    void theNextCurfewIsAlwaysAhead()
    {
        const QDateTime start(QDate(2026, 9, 27), QTime(0, 0));
        for (int minutes = 0; minutes < 7 * 24 * 60; minutes += 7) {
            const QDateTime now = start.addSecs(minutes * 60);
            const qint64 left = now.secsTo(curfewFor(curfewNight(now)));
            QVERIFY2(left > 0 && left <= 25 * 3600, qPrintable(now.toString(Qt::ISODate)));
        }
    }
};

CEDARVIEW_TEST_MAIN(TestCurfew, QCoreApplication)
#include "tst_curfew.moc"
