// The academic calendar, at its edges.
//
// Every bug this can have is a boundary bug — off by one at the start of term,
// off by one at the end, or the wrong answer in the gap between them. The
// countdown is also the one figure on the summary screen that cannot be checked
// against a live service, so this is the only thing standing behind it.
//
// Note what is *not* asserted: the exact number of days left today. That test
// would pass once and then fail every day afterwards.

#include "testsupport.h"

#include "core/calendar.h"

using namespace mycu;

class TestCalendar : public QObject
{
    Q_OBJECT

private slots:
    // ---- Which term a date falls in ------------------------------------------

    void whichTermADayFallsIn_data()
    {
        QTest::addColumn<QDate>("day");
        QTest::addColumn<QString>("expected");
        // Fall: Aug 19 -> Dec 11, both inclusive.
        QTest::newRow("before fall") << QDate(2026, 8, 18) << "";
        QTest::newRow("fall starts") << QDate(2026, 8, 19) << "Fall";
        QTest::newRow("mid fall") << QDate(2026, 10, 1) << "Fall";
        QTest::newRow("fall ends") << QDate(2026, 12, 11) << "Fall";
        QTest::newRow("after fall") << QDate(2026, 12, 12) << "";
        // Spring: Jan 5 -> Apr 30, both inclusive.
        QTest::newRow("before spring") << QDate(2027, 1, 4) << "";
        QTest::newRow("spring starts") << QDate(2027, 1, 5) << "Spring";
        QTest::newRow("spring ends") << QDate(2027, 4, 30) << "Spring";
        QTest::newRow("after spring") << QDate(2027, 5, 1) << "";
        // The two breaks, well inside.
        QTest::newRow("christmas") << QDate(2026, 12, 25) << "";
        QTest::newRow("summer") << QDate(2027, 7, 4) << "";
    }
    void whichTermADayFallsIn()
    {
        QFETCH(QDate, day);
        QFETCH(QString, expected);
        const auto term = currentTerm(day);
        QCOMPARE(term ? term->name : QString(), expected);
    }

    // ---- Days left -----------------------------------------------------------

    // Not one. On Dec 11 the semester is over today, not tomorrow.
    void theLastDayOfTermHasZeroDaysLeft() { QCOMPARE(FALL.daysLeft(QDate(2026, 12, 11)), 0); }

    void theDayBeforeTheEndHasOneDayLeft() { QCOMPARE(FALL.daysLeft(QDate(2026, 12, 10)), 1); }

    // Sep 18 -> Dec 11 is 84 days, weekends and Thanksgiving included. The card
    // says "days left", and this is what a reader would count on a wall
    // calendar.
    void daysLeftCountsCalendarDaysNotClassDays() { QCOMPARE(FALL.daysLeft(QDate(2026, 9, 18)), 84); }

    // Belt and braces: the viewmodel only asks while in term, but the floor
    // means a caller that does not check cannot produce "-12 days left".
    void daysLeftNeverGoesNegative() { QCOMPARE(FALL.daysLeft(QDate(2026, 12, 31)), 0); }

    void springIsMeasuredInsideItsOwnYear() { QCOMPARE(SPRING.daysLeft(QDate(2027, 4, 1)), 29); }

    // ---- The bar -------------------------------------------------------------

    // "N/N days left" on the first day, "0/N" on the last.
    void theCountdownStartsAtTheTotal()
    {
        QCOMPARE(FALL.daysLeft(QDate(2026, 8, 19)), FALL.totalDays(2026));
        QCOMPARE(FALL.totalDays(2026), 114);
        QCOMPARE(SPRING.daysLeft(QDate(2027, 1, 5)), SPRING.totalDays(2027));
    }

    void theFractionIsEmptyOnTheFirstDayAndFullOnTheLast()
    {
        QCOMPARE(FALL.elapsedFraction(QDate(2026, 8, 19)), 0.0);
        QCOMPARE(FALL.elapsedFraction(QDate(2026, 12, 11)), 1.0);
    }

    void theFractionIsClampedOutsideTheTerm()
    {
        for (const QDate day : {QDate(2026, 6, 1), QDate(2026, 12, 31)}) {
            const double f = FALL.elapsedFraction(day);
            QVERIFY(f >= 0.0 && f <= 1.0);
        }
    }

    // The direction matters more than any single value: the semester bar shows
    // how much is done, so it fills over the term.
    void theFractionRisesAsTheTermRuns()
    {
        double last = -1.0;
        for (int month : {9, 10, 11, 12}) {
            const double f = FALL.elapsedFraction(QDate(2026, month, 1));
            QVERIFY(f >= last);
            last = f;
        }
    }

    // ---- Between terms -------------------------------------------------------

    void thereIsNoNextTermWhileYouAreInOne() { QVERIFY(!nextTermStart(QDate(2026, 10, 1))); }

    // The one case a naive implementation gets wrong.
    void theChristmasBreakLooksForwardIntoTheNextYear()
    {
        const auto upcoming = nextTermStart(QDate(2026, 12, 20));
        QVERIFY(upcoming);
        QCOMPARE(upcoming->first.name, QStringLiteral("Spring"));
        QCOMPARE(upcoming->second, QDate(2027, 1, 5));
    }

    void theFirstDaysOfJanuaryStillPointAtThisYearsSpring()
    {
        const auto upcoming = nextTermStart(QDate(2027, 1, 1));
        QVERIFY(upcoming);
        QCOMPARE(upcoming->first.name, QStringLiteral("Spring"));
        QCOMPARE(upcoming->second, QDate(2027, 1, 5));
    }

    void theSummerPointsAtTheFall()
    {
        const auto upcoming = nextTermStart(QDate(2027, 5, 15));
        QVERIFY(upcoming);
        QCOMPARE(upcoming->first.name, QStringLiteral("Fall"));
        QCOMPARE(upcoming->second, QDate(2027, 8, 19));
    }

    // Every day of a leap year belongs to at most one term.
    void theTwoTermsDoNotOverlapAnywhereInAYear()
    {
        for (QDate day(2028, 1, 1); day.year() == 2028; day = day.addDays(1)) {
            const int matches = int(FALL.contains(day)) + int(SPRING.contains(day));
            QVERIFY2(matches <= 1, qPrintable(day.toString(Qt::ISODate)));
        }
    }
};

CEDARVIEW_TEST_MAIN(TestCalendar, QCoreApplication)
#include "tst_calendar.moc"
