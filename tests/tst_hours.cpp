// Opening hours: the hand-entered tables (core/hours.h), and the Hours and
// Campus screens built on them.
//
// The clock is always passed in. The days used: Wed Sep 16 and Thu Sep 17
// 2026 (the canvas's two days), Fri Oct 2 and Sat Oct 3, Sun Oct 4.

#include "testsupport.h"

#include "core/hours.h"
#include "ui/viewmodels/campus.h"
#include "ui/viewmodels/curfew.h"
#include "ui/viewmodels/hours.h"

using namespace mycu;
using namespace mycu::hours;

namespace {

QDateTime at(int month, int day, int hour, int minute = 0)
{
    return QDateTime(QDate(2026, month, day), QTime(hour, minute));
}

const Building &building(const QString &name)
{
    for (const Building &b : buildings()) {
        if (b.name == name)
            return b;
    }
    qFatal("no building %s", qPrintable(name));
}

Schedule scheduleOf(const Building &b)
{
    return [b](QDate day) { return b.on(day); };
}

QStringList names(const QVariantList &list)
{
    QStringList out;
    for (const QVariant &item : list)
        out.append(item.toMap().value("name").toString());
    return out;
}

} // namespace

class TestHours : public QObject
{
    Q_OBJECT

private slots:
    // ---- The tables ----------------------------------------------------------

    void everyBuildingFromThePageIsThere()
    {
        QCOMPARE(buildings().size(), 19);
        QCOMPARE(building("Centennial Library").code, QStringLiteral("LB"));
        QCOMPARE(building("Milner Hall").code, QString());
    }

    // The two cells not taken literally; see core/hours.h.
    void theTwoFlaggedCellsAreReadAsDocumented()
    {
        const Building &bts = building("Center for Biblical and Theological Studies");
        QCOMPARE(bts.hours[static_cast<int>(BuildingDay::Friday)]->open, 390); // 6:30 AM, not PM
        const Building &alford = building("Alford Auditorium");
        QVERIFY(!alford.hours[static_cast<int>(BuildingDay::Saturday)]);
        QVERIFY(alford.note.contains("Authorized access"));
    }

    void theDayTypesFollowEachPage()
    {
        QCOMPARE(buildingDayOf(QDate(2026, 10, 1)), BuildingDay::MonThu);
        QCOMPARE(buildingDayOf(QDate(2026, 10, 2)), BuildingDay::Friday);
        QCOMPARE(diningDayOf(QDate(2026, 10, 2)), DiningDay::Weekday);
        QCOMPARE(diningDayOf(QDate(2026, 10, 3)), DiningDay::Saturday);
        QCOMPARE(diningDayOf(QDate(2026, 10, 4)), DiningDay::Sunday);
    }

    void theCommonsSittingsChangeAtTheWeekend()
    {
        QCOMPARE(commonsSittings(DiningDay::Weekday).size(), 4);
        QCOMPARE(commonsSittings(DiningDay::Saturday).at(1).name, QStringLiteral("Brunch"));
        QCOMPARE(commonsSittings(DiningDay::Saturday).at(1).slot, QStringLiteral("lunch"));
    }

    // ---- Asking about the clock ---------------------------------------------

    // 1485 is 12:45 AM: Friday's hours hold past midnight into Saturday.
    void aLateNightRunsPastMidnight()
    {
        const Schedule app = scheduleOf(building("Apple Technology Resource Center"));
        QVERIFY(isOpen(app, at(10, 3, 0, 30)));
        QCOMPARE(*closesAt(app, at(10, 3, 0, 30)), at(10, 3, 0, 45));
        QVERIFY(!isOpen(app, at(10, 3, 0, 50)));
        // A weeknight's 11:45 PM close does not carry over.
        QVERIFY(!isOpen(app, at(10, 2, 0, 30)));
    }

    void theNextOpeningLooksAhead()
    {
        const Schedule library = scheduleOf(building("Centennial Library"));
        QVERIFY(!isOpen(library, at(10, 4, 10, 0))); // Sunday morning
        QCOMPARE(nextOpening(library, at(10, 4, 10, 0))->opens, at(10, 4, 15, 30));
        // Saturday evening, after it closed at 7 PM: Sunday afternoon.
        QCOMPARE(nextOpening(library, at(10, 3, 20, 0))->opens, at(10, 4, 15, 30));
    }

    void theCommonsIsOpenDuringAnySitting()
    {
        QVERIFY(isOpen(commonsOn, at(9, 17, 12, 0)));
        QVERIFY(!isOpen(commonsOn, at(9, 17, 9, 42)));
        QCOMPARE(nextOpening(commonsOn, at(9, 17, 9, 42))->opens, at(9, 17, 10, 30));
    }

    // ---- The Hours section ---------------------------------------------------

    // The canvas's 9:42 AM on a Thursday: breakfast over, lunch in 48 min.
    void midMorningLeadsWithWhatIsOpenAndWhatOpensNext()
    {
        HoursViewModel vm;
        vm.now = [] { return at(9, 17, 9, 42); };
        vm.refreshAll();

        QVERIFY(vm.anyOpen());
        QCOMPARE(vm.openName(), QStringLiteral("Rinnova"));
        QCOMPARE(vm.openUntil(), QStringLiteral("until 7:00 PM"));
        QCOMPARE(vm.nextOpeningText(), QStringLiteral("Opening at 10:30 AM"));
        QCOMPARE(vm.nextOpeningIn(), QStringLiteral("in 48 min"));
        QCOMPARE(vm.nextOpeningNames(),
                 (QStringList{"Commons lunch", "The Commons Market", "Chick-fil-A / Tossed", "Panda Express",
                              "The Cafe", "Grab+Go Market (BTS)"}));
        QCOMPARE(vm.todayTypeText(), QStringLiteral("Weekday hours"));
        QVERIFY(qAbs(vm.nowFraction() - (582 - 420) / 1020.0) < 1e-9);
    }

    void theTimelineMarksPastNowAndNext()
    {
        HoursViewModel vm;
        vm.now = [] { return at(9, 17, 9, 42); };
        vm.refreshAll();
        auto *model = static_cast<QAbstractItemModel *>(vm.timeline());
        const auto role = [&](const char *name) { return model->roleNames().key(name); };

        QCOMPARE(model->data(model->index(0, 0), role("name")).toString(), QStringLiteral("The Commons"));
        QCOMPARE(model->data(model->index(0, 0), role("status")).toString(), QStringLiteral("Lunch at 10:30 AM"));
        QStringList kinds;
        for (const QVariant &segment : model->data(model->index(0, 0), role("segments")).toList())
            kinds.append(segment.toMap().value("kind").toString());
        QCOMPARE(kinds, (QStringList{"past", "past", "next", "next"}));

        // Rinnova is open; Chick-fil-A's day ends on card-only time.
        for (int r = 0; r < model->rowCount(); ++r) {
            const QString name = model->data(model->index(r, 0), role("name")).toString();
            const QVariantList segments = model->data(model->index(r, 0), role("segments")).toList();
            if (name == u"Rinnova") {
                QVERIFY(model->data(model->index(r, 0), role("live")).toBool());
                QCOMPARE(segments.first().toMap().value("kind").toString(), QStringLiteral("now"));
            }
            if (name == u"Chick-fil-A / Tossed")
                QCOMPARE(segments.last().toMap().value("kind").toString(), QStringLiteral("flexOnly"));
        }
    }

    // Hot breakfast hands straight over to continental: one opening.
    void backToBackSittingsReadAsOneOpening()
    {
        HoursViewModel vm;
        vm.now = [] { return at(9, 17, 7, 30); };
        vm.refreshAll();
        QCOMPARE(vm.openName(), QStringLiteral("The Commons"));
        QCOMPARE(vm.openUntil(), QStringLiteral("until 9:30 AM"));
    }

    void anotherDayTypeShowsPlainHours()
    {
        HoursViewModel vm;
        vm.now = [] { return at(9, 17, 9, 42); };
        vm.refreshAll();
        vm.setDayType(1);
        QVERIFY(!vm.showingToday());
        QCOMPARE(vm.nowFraction(), -1.0);
        auto *model = static_cast<QAbstractItemModel *>(vm.timeline());
        QCOMPARE(model->data(model->index(0, 0), model->roleNames().key("status")).toString(),
                 QStringLiteral("8:00 AM – 6:30 PM"));
        for (const QVariant &period : vm.swipePeriods())
            QVERIFY(!period.toMap().value("current").toBool());
    }

    void theCurrentSwipePeriodIsFlagged()
    {
        HoursViewModel vm;
        vm.now = [] { return at(9, 17, 9, 42); };
        vm.refreshAll();
        QStringList current;
        for (const QVariant &period : vm.swipePeriods()) {
            if (period.toMap().value("current").toBool())
                current.append(period.toMap().value("name").toString());
        }
        QCOMPARE(current, QStringList{"Breakfast"});
    }

    // ---- The Campus tab ------------------------------------------------------

    // The canvas's 10:48 PM on a Wednesday.
    void buildingsAreGroupedByWhenTheyClose()
    {
        CampusViewModel vm;
        vm.now = [] { return at(9, 16, 22, 48); };
        vm.refreshAll();

        const QVariantList groups = vm.groups();
        QCOMPARE(groups.size(), 4);
        const QVariantMap first = groups.first().toMap();
        QCOMPARE(first.value("title").toString(), QStringLiteral("Closes at 11:00 PM"));
        QCOMPARE(first.value("badge").toString(), QStringLiteral("in 12 min"));
        QVERIFY(first.value("soon").toBool());
        QCOMPARE(first.value("buildings").toList().size(), 8);
        QCOMPARE(groups.at(1).toMap().value("title").toString(), QStringLiteral("Closes at 11:30 PM"));
        QCOMPARE(names(groups.at(1).toMap().value("buildings").toList()),
                 (QStringList{"Centennial Library", "Chick-fil-A"}));

        const QVariantMap closed = groups.last().toMap();
        QVERIFY(closed.value("closed").toBool());
        QCOMPARE(closed.value("badge").toString(), QStringLiteral("Opens 7:00 AM"));
        QCOMPARE(names(closed.value("buildings").toList()),
                 (QStringList{"Chemistry Lab Center", "Civil Engineering Center"}));

        QCOMPARE(vm.openCount(), 17);
        QCOMPARE(vm.closingSoonCount(), 8);
        QCOMPARE(vm.closedCount(), 2);
    }

    void aFilterNarrowsAndTappingItAgainClearsIt()
    {
        CampusViewModel vm;
        vm.now = [] { return at(9, 16, 22, 48); };
        vm.refreshAll();
        vm.setFilter(3);
        QCOMPARE(vm.groups().size(), 1);
        vm.setFilter(3);
        QCOMPARE(vm.filter(), 0);
        QCOMPARE(vm.groups().size(), 4);
    }

    void searchMatchesNamesAndCodes()
    {
        CampusViewModel vm;
        vm.now = [] { return at(9, 16, 22, 48); };
        vm.refreshAll();
        vm.setQuery("lib");
        QCOMPARE(vm.groups().size(), 1);
        QCOMPARE(names(vm.groups().first().toMap().value("buildings").toList()), QStringList{"Centennial Library"});
        vm.setQuery("ssc");
        QCOMPARE(names(vm.groups().first().toMap().value("buildings").toList()),
                 QStringList{"Stevens Student Center"});
    }

    void starredBuildingsAreListedWithTheirTimes()
    {
        CampusViewModel vm;
        vm.now = [] { return at(9, 16, 22, 48); };
        vm.refreshAll();
        vm.toggleFavorite("Fitness Recreation Center");
        const QVariantList favorites = vm.favoriteBuildings();
        QCOMPARE(favorites.size(), 1);
        const QVariantMap ftr = favorites.first().toMap();
        QCOMPARE(ftr.value("closesText").toString(), QStringLiteral("Closes 11:00 PM"));
        QCOMPARE(ftr.value("closesIn").toString(), QStringLiteral("12 min"));
        QVERIFY(ftr.value("closingSoon").toBool());
        QVERIFY(ftr.value("favorite").toBool());
        vm.toggleFavorite("Fitness Recreation Center");
        QVERIFY(vm.favoriteBuildings().isEmpty());
    }

    // ---- Curfew's evening bar ------------------------------------------------

    void theEveningBarRunsFromEightToCurfew()
    {
        CurfewViewModel vm;
        vm.now = [] { return at(9, 16, 22, 48); };
        vm.refreshAll();
        QVERIFY(qAbs(vm.eveningFraction() - 168.0 / 239.0) < 1e-9);
        QCOMPARE(vm.timeLeftText(), QStringLiteral("1h 11m"));
        QCOMPARE(vm.nowText(), QStringLiteral("10:48"));
        QCOMPARE(vm.ruleText(), QStringLiteral("Sunday–Thursday curfew"));

        vm.now = [] { return at(9, 16, 19, 0); };
        vm.refreshAll();
        QCOMPARE(vm.eveningFraction(), -1.0);
        QCOMPARE(vm.timeLeftText(), QString());
    }

    // Saturday 12:30 AM is still Friday night, an hour-long tail past midnight.
    void aLateNightKeepsItsEvening()
    {
        CurfewViewModel vm;
        vm.now = [] { return at(10, 3, 0, 30); };
        vm.refreshAll();
        QCOMPARE(vm.timeLeftText(), QStringLiteral("29m"));
        QVERIFY(vm.eveningFraction() > 0.8 && vm.eveningFraction() < 1.0);
    }
};

CEDARVIEW_TEST_MAIN(TestHours, QCoreApplication)
#include "tst_hours.moc"
