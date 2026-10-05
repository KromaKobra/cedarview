// Viewmodel behaviour, headless.
//
// These touch Qt (offscreen) but never run a worker thread: the completion
// handlers are invoked directly, which is what makes the tests deterministic.
// The threading itself is ui/tasks.h and has its own suite.

#include "testsupport.h"

#include "core/providers/chapel.h"
#include "core/providers/chapel_schedule.h"
#include "core/providers/meals.h"
#include "ui/viewmodels/chapel.h"
#include "ui/viewmodels/curfew.h"
#include "ui/viewmodels/dining.h"
#include "ui/viewmodels/format.h"

#include <QSignalSpy>
#include <QUuid>

namespace mycu {

namespace {

QDate today() { return QDate::currentDate(); }

QDateTime local(int y, int mo, int d, int h = 10, int mi = 0)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi));
}

template <typename E>
std::exception_ptr make(const char *message)
{
    return std::make_exception_ptr(E(QString::fromUtf8(message)));
}

UpcomingChapel chapel(QDateTime when, const QString &title, const QStringList &speakers = {},
                      const QString &description = {}, bool livestream = false)
{
    return UpcomingChapel{when, title, speakers, description, livestream};
}

ChapelSummary figures(std::optional<int> used, std::optional<int> total, std::optional<int> remaining)
{
    ChapelSummary s;
    s.used = used;
    s.total = total;
    s.remaining = remaining;
    return s;
}

ChapelLedgerEntry entry(QDateTime on, int count, const QString &type = {}, const QString &reason = {})
{
    ChapelLedgerEntry e;
    e.on = on;
    e.count = count;
    e.entryType = type;
    e.reason = reason;
    return e;
}

MenuBlock block(const QString &meal, const QString &slot, const QList<MenuItem> &items,
                const QString &venue = HOME_COOKING)
{
    return MenuBlock{venue, meal, slot, items};
}

// Home Cooking's breakfast, which is the sitting the dining viewmodel shows
// first at the 7 AM these tests are pinned to.
DayMenu home(QDate on, const QStringList &dishes)
{
    QList<MenuItem> items;
    for (const QString &d : dishes)
        items.append(MenuItem{d, {}});
    return DayMenu{on, {block("Breakfast", "breakfast", items)}};
}

QVariant cell(QAbstractItemModel *model, int row, int role)
{
    return model->data(model->index(row, 0), role);
}

// The dishes the Menu section shows, without the station headers.
QStringList texts(DiningViewModel &vm)
{
    QStringList out;
    auto *model = static_cast<QAbstractItemModel *>(vm.stations());
    for (int r = 0; r < model->rowCount(); ++r) {
        if (cell(model, r, StationListModel::RowTypeRole).toString() != u"header")
            out.append(cell(model, r, StationListModel::TextRole).toString());
    }
    return out;
}

// Each station row as [rowType, station, text, hiddenText, isLast].
QList<QVariantList> stationRows(DiningViewModel &vm)
{
    auto *model = static_cast<QAbstractItemModel *>(vm.stations());
    QList<QVariantList> rows;
    for (int r = 0; r < model->rowCount(); ++r) {
        rows.append({cell(model, r, StationListModel::RowTypeRole), cell(model, r, StationListModel::StationRole),
                     cell(model, r, StationListModel::TextRole), cell(model, r, StationListModel::HiddenTextRole),
                     cell(model, r, StationListModel::LastRole)});
    }
    return rows;
}

// Each schedule row as a list of the named roles.
QList<QVariantList> scheduleRows(ChapelViewModel &vm, const QList<QByteArray> &roles)
{
    auto *model = static_cast<QAbstractItemModel *>(vm.schedule());
    const auto names = model->roleNames();
    QList<QVariantList> rows;
    for (int r = 0; r < model->rowCount(); ++r) {
        QVariantList row;
        for (const QByteArray &role : roles)
            row.append(cell(model, r, names.key(role)));
        rows.append(row);
    }
    return rows;
}

QDateTime atDaysAgo(int daysAgo, int hour, int minute = 0)
{
    return QDateTime(today().addDays(-daysAgo), QTime(hour, minute));
}

MealTransaction txn(QDateTime at, const QString &activity, const QString &period = {},
                    std::optional<double> amount = std::nullopt, bool deposit = false)
{
    return MealTransaction{at, activity, period, amount, deposit};
}

QList<MealTransaction> activityFixture()
{
    return {
        txn(atDaysAgo(0, 12, 25), "Board meal", "Lunch"),
        txn(atDaysAgo(0, 0, 5), "Flex purchase", {}, 3.74),
        txn(atDaysAgo(1, 17, 45), "Meal exchange", "Dinner"),
        txn(atDaysAgo(1, 12, 0), "Flex purchase", "Lunch", 6.0),
        txn(atDaysAgo(3, 7, 40), "Board meal", "Breakfast"),
    };
}

// A captured menu fetch, never run: the provider it was asked for, and its
// two answers.
struct Fetch
{
    DiningProvider provider;
    DiningViewModel::MenusDone done;
    DiningViewModel::MenusFailed failed;
};

} // namespace

class TestViewModels : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    QString freshDir() { return m_dir.path() + "/" + QUuid::createUuid().toString(QUuid::Id128); }

    std::unique_ptr<ChapelViewModel> chapelVm()
    {
        return std::make_unique<ChapelViewModel>(std::make_shared<FixtureTransport>(testing::fixturesDir()),
                                                 Storage::at(freshDir()));
    }

    // Pinned to breakfast time: which sitting is "next" is a function of the
    // hour, and a test that only passes before 10:30am is not a test.
    std::unique_ptr<DiningViewModel> diningVm(QList<Fetch> *fetches = nullptr)
    {
        auto vm = std::make_unique<DiningViewModel>(std::make_shared<FixtureTransport>(testing::fixturesDir()));
        vm->now = [] { return QDateTime(today(), QTime(7, 0)); };
        if (fetches) {
            vm->fetchMenus = [fetches](DiningProvider p, DiningViewModel::MenusDone d,
                                       DiningViewModel::MenusFailed f) {
                fetches->append({p, d, f});
            };
        }
        return vm;
    }

    QList<QVariantList> activityRows(DiningViewModel &vm)
    {
        auto *model = static_cast<QAbstractItemModel *>(vm.activity());
        QList<QVariantList> rows;
        for (int r = 0; r < model->rowCount(); ++r) {
            rows.append({cell(model, r, ActivityListModel::HeaderRole), cell(model, r, ActivityListModel::TitleRole),
                         cell(model, r, ActivityListModel::DetailRole), cell(model, r, ActivityListModel::AmountRole)});
        }
        return rows;
    }

private slots:
    // ---- List model ----------------------------------------------------------

    void rolesAreNamedForQml()
    {
        ChapelListModel model;
        QSet<QByteArray> names;
        for (const QByteArray &n : model.roleNames())
            names.insert(n);
        QCOMPARE(names, (QSet<QByteArray>{"whenText", "reason", "entryType", "count", "isSkip", "title", "detail"}));
    }

    void aSkipRowExposesItsFields()
    {
        ChapelListModel model;
        model.replace({entry(local(2026, 8, 20), 1, "Chapel Skip", "Absent from Chapel 8/20/2026")});
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(cell(&model, 0, ChapelListModel::CountRole).toInt(), 1);
        QCOMPARE(cell(&model, 0, ChapelListModel::SkipRole).toBool(), true);
        QCOMPARE(cell(&model, 0, ChapelListModel::TypeRole).toString(), QStringLiteral("Chapel Skip"));
        QVERIFY(cell(&model, 0, ChapelListModel::WhenRole).toString().contains("Aug"));
    }

    // It gave a skip back; it must not render like another absence.
    void aManualAdjustmentKeepsItsNegativeCount()
    {
        ChapelListModel model;
        model.replace({entry({}, -1, "Manual Adjustment", "Had ID replaced")});
        QCOMPARE(cell(&model, 0, ChapelListModel::CountRole).toInt(), -1);
        QCOMPARE(cell(&model, 0, ChapelListModel::SkipRole).toBool(), false);
    }

    void anUndatedEntryShowsItsReasonInsteadOfADate()
    {
        ChapelListModel model;
        model.replace({entry({}, -1, "Manual Adjustment", "Had ID replaced")});
        QCOMPARE(cell(&model, 0, ChapelListModel::WhenRole).toString(), QStringLiteral("Had ID replaced"));
        // …and the reason is not then repeated on the second line.
        QCOMPARE(cell(&model, 0, ChapelListModel::ReasonRole).toString(), QString());
    }

    void outOfRangeAccessReturnsNothing()
    {
        ChapelListModel model;
        QVERIFY(!model.data(model.index(5, 0), ChapelListModel::WhenRole).isValid());
    }

    // ---- Viewmodel -----------------------------------------------------------

    void startsEmptyAndNotLoaded()
    {
        auto vm = chapelVm();
        QCOMPARE(static_cast<QAbstractItemModel *>(vm->records())->rowCount(), 0);
        QCOMPARE(vm->loaded(), false);
        QCOMPARE(vm->busy(), false);
        QCOMPARE(vm->error(), QString());
    }

    void aSuccessfulLoadPopulatesEverything()
    {
        auto vm = chapelVm();
        ChapelSummary s = figures(2, 18, 16);
        s.term = "2026FA";
        s.termName = "Fall Semester 2026";
        s.entries = {entry(local(2026, 8, 20), 1)};
        vm->onLoaded(s);

        QCOMPARE(vm->loaded(), true);
        QCOMPARE(vm->busy(), false);
        QCOMPARE(vm->term(), QStringLiteral("Fall Semester 2026"));
        QCOMPARE(vm->used(), 2);
        QCOMPARE(vm->allowed(), 18);
        QCOMPARE(vm->remaining(), 16);
        QCOMPARE(static_cast<QAbstractItemModel *>(vm->records())->rowCount(), 1);
    }

    // The ledger sums to 1 here; the server says 2. The server wins.
    void figuresArePassedThroughNotRecomputed()
    {
        auto vm = chapelVm();
        ChapelSummary s = figures(2, 18, 16);
        s.entries = {entry({}, 1), entry({}, -1), entry({}, 1)};
        vm->onLoaded(s);
        QCOMPARE(vm->used(), 2);
    }

    // QML has no null int, and 0 would render as "0 of 0 skips" — a lie.
    void unknownFiguresAreReportedAsMinusOne()
    {
        auto vm = chapelVm();
        vm->onLoaded(ChapelSummary());
        QCOMPARE(vm->used(), -1);
        QCOMPARE(vm->allowed(), -1);
        QCOMPARE(vm->remaining(), -1);
    }

    void theAllowanceBreakdownExplainsAnUnexpectedTotal()
    {
        auto vm = chapelVm();
        ChapelSummary s = figures(2, 18, 16);
        s.allowance = {{"Skips Allowed", 17, {}}, {"Manual Arrangement", 1, {}}};
        vm->onLoaded(s);
        QCOMPARE(vm->allowanceText(), QStringLiteral("17 skips allowed + 1 manual arrangement"));
    }

    void aSingleAllowanceLineNeedsNoExplanation()
    {
        auto vm = chapelVm();
        ChapelSummary s = figures(0, 17, std::nullopt);
        s.allowance = {{"Skips Allowed", 17, {}}};
        vm->onLoaded(s);
        QCOMPARE(vm->allowanceText(), QString());
    }

    // Re-authenticating is part of the lifecycle, not a failure to report.
    void expiryIsSignalledAndNeverShownAsAnError()
    {
        auto vm = chapelVm();
        QSignalSpy spy(vm.get(), &ChapelViewModel::sessionExpired);
        vm->onFailed(make<SessionExpired>("gone"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(vm->error(), QString());
        QCOMPARE(vm->busy(), false);
    }

    void aParseErrorTellsYouWhereToLook()
    {
        auto vm = chapelVm();
        vm->onFailed(make<ParseError>("no table"));
        QVERIFY(vm->error().contains("check-live"));
    }

    void aTransportErrorIsReportedAsReachability()
    {
        auto vm = chapelVm();
        vm->onFailed(make<TransportError>("timed out"));
        QVERIFY(vm->error().contains("Couldn't reach"));
        QVERIFY(vm->error().contains("timed out"));
    }

    void anUnexpectedExceptionStillReachesTheUser()
    {
        auto vm = chapelVm();
        vm->onFailed(std::make_exception_ptr(std::runtime_error("boom")));
        QVERIFY(vm->error().contains("Unexpected error"));
        QVERIFY(vm->error().contains("boom"));
    }

    void aSuccessfulLoadClearsAPreviousError()
    {
        auto vm = chapelVm();
        vm->onFailed(make<TransportError>("timed out"));
        QVERIFY(!vm->error().isEmpty());
        vm->onLoaded(figures(0, 18, 18));
        QCOMPARE(vm->error(), QString());
    }

    // A second tap on ↻ while busy must not start a second fetch.
    void refreshIsNotReEntrant()
    {
        auto vm = chapelVm();
        vm->m_busy = true;
        QSignalSpy spy(vm.get(), &ChapelViewModel::changed);
        vm->refresh();
        QCOMPARE(spy.count(), 0); // no state change emitted, so nothing was kicked off
    }

    void theTermIsRememberedAcrossLaunches()
    {
        const QString dir = m_dir.path() + "/remember";
        ChapelViewModel first(std::make_shared<FixtureTransport>(testing::fixturesDir()), Storage::at(dir));
        ChapelSummary s;
        s.termName = "Fall Semester 2026";
        first.onLoaded(s);

        ChapelViewModel second(std::make_shared<FixtureTransport>(testing::fixturesDir()), Storage::at(dir));
        QCOMPARE(second.term(), QStringLiteral("Fall Semester 2026"));
    }

    // ---- What the summary screen reads --------------------------------------

    void theSkipBarIsAFractionOfTheAllowance()
    {
        auto vm = chapelVm();
        vm->onLoaded(figures(2, 18, 16));
        QCOMPARE(vm->remainingFraction(), 16.0 / 18.0);
    }

    void theBarIsEmptyRatherThanWrongWhenTheFiguresAreMissing()
    {
        auto vm = chapelVm();
        vm->onLoaded(ChapelSummary());
        QCOMPARE(vm->remainingFraction(), 0.0);
    }

    // The server's two halves of arithmetic are not guaranteed to agree. A bar
    // past the end of its track looks broken in a way a full bar does not.
    void theBarNeverOverflowsItsTrack()
    {
        auto vm = chapelVm();
        vm->onLoaded(figures(0, 4, 9));
        QCOMPARE(vm->remainingFraction(), 1.0);
    }

    // Two pieces of text on the card, so two properties. Slicing the badge back
    // out of a formatted "Tomorrow 10:00 AM" is how a UI ends up rendering a
    // time inside a pill.
    void theDayBadgeAndTheDateLineAreSeparate()
    {
        auto vm = chapelVm();
        vm->m_next = chapel(QDateTime(today().addDays(1), QTime(10, 0)), "Worship Chapel");
        QCOMPARE(vm->nextChapelDay(), QStringLiteral("Tomorrow"));
        QVERIFY(vm->nextChapelDateText().endsWith("10:00 AM"));
        QVERIFY(!vm->nextChapelDateText().contains("Tomorrow"));
        QCOMPARE(vm->nextChapelWhen(), QStringLiteral("Tomorrow 10:00 AM"));
    }

    void theDateLineReadsLikeADate()
    {
        auto vm = chapelVm();
        vm->m_next = chapel(local(2026, 9, 18), "x");
        QCOMPARE(vm->nextChapelDateText(), QStringLiteral("Fri, Sep 18 · 10:00 AM"));
    }

    void todayAndAWeekdayAreBothSpelledOut()
    {
        auto vm = chapelVm();
        vm->m_next = chapel(QDateTime(today(), QTime(10, 0)), {});
        QCOMPARE(vm->nextChapelDay(), QStringLiteral("Today"));

        const QDate later = today().addDays(4);
        vm->m_next = chapel(QDateTime(later, QTime(10, 0)), {});
        QCOMPARE(vm->nextChapelDay(), QLocale::c().toString(later, "dddd"));
    }

    // Over the summer there genuinely is no next chapel, and "TBA" is a claim.
    void noUpcomingChapelRendersAsNothingAtAll()
    {
        auto vm = chapelVm();
        QCOMPARE(vm->nextChapelDay(), QString());
        QCOMPARE(vm->nextChapelDateText(), QString());
        QCOMPARE(vm->nextSpeaker(), QString());
    }

    void midnightAndNoonAreNotRenderedAsZeroAndTwelve()
    {
        auto vm = chapelVm();
        vm->m_next = chapel(QDateTime(today(), QTime(0, 5)), {});
        QVERIFY(vm->nextChapelDateText().contains("12:05 AM"));
        vm->m_next = chapel(QDateTime(today(), QTime(12, 0)), {});
        QVERIFY(vm->nextChapelDateText().contains("12:00 PM"));
    }

    // ---- The schedule (the Chapel tab) --------------------------------------

    void theScheduleIsGroupedByWeek()
    {
        auto vm = scheduled();
        QCOMPARE(scheduleRows(*vm, {"isHeader", "heading", "who"}),
                 (QList<QVariantList>{
                     {true, "This week", ""},
                     {false, "", "Garrett Kell"},
                     {false, "", "SGA"},
                     {true, "Next week", ""},
                     {false, "", "Philip Miller"},
                     {true, "Week of Oct 5", ""},
                     {false, "", "Majors Assembly"},
                 }));
    }

    void aFinishedChapelIsGoneAndTheCurrentOneIsMarkedNow()
    {
        auto vm = scheduled();
        QList<QVariantList> chapels;
        for (const auto &row : scheduleRows(*vm, {"isHeader", "dayName", "dayNumber", "badge", "isNow"})) {
            if (!row[0].toBool())
                chapels.append(row.mid(1));
        }
        QCOMPARE(chapels[0], (QVariantList{"WED", "23", "Now", true}));
        QCOMPARE(chapels[1], (QVariantList{"THU", "24", "Tomorrow", false}));
        QCOMPARE(chapels[2].mid(2), (QVariantList{"", false}));
    }

    void theTitleIsShownOnlyWhenItAddsSomething()
    {
        auto vm = scheduled();
        QStringList subtitles;
        for (const auto &row : scheduleRows(*vm, {"isHeader", "subtitle"})) {
            if (!row[0].toBool())
                subtitles.append(row[1].toString());
        }
        // Same as the speaker; an unnamed chapel whose title *is* its name; a
        // real sermon title; an unnamed assembly.
        QCOMPARE(subtitles, (QStringList{"", "", "Sermon on the Mount", ""}));
    }

    void timeDescriptionAndLivestreamComeThrough()
    {
        auto vm = scheduled();
        QList<QVariantList> rows;
        for (const auto &row : scheduleRows(*vm, {"isHeader", "timeText", "description", "livestream"})) {
            if (!row[0].toBool())
                rows.append(row.mid(1));
        }
        QCOMPARE(rows.first(), (QVariantList{"10:00 AM", "Lead pastor of Del Ray.", true}));
        QCOMPARE(rows.last(), (QVariantList{"11:00 AM", "", false}));
    }

    void theSummaryStillGetsTheNextChapelFromTheFullSchedule()
    {
        auto vm = chapelVm();
        const QDateTime now = QDateTime::currentDateTime();
        // Soonest first, as fetchSchedule delivers it. The first has started,
        // so it is on the Chapel tab as "Now" but is not the summary's *next*
        // chapel.
        vm->onScheduleLoaded({
            chapel(now.addSecs(-5 * 60), "Started", {"Started Speaker"}),
            chapel(now.addDays(1), "Sooner", {"Sooner Speaker"}),
        });
        QCOMPARE(vm->nextSpeaker(), QStringLiteral("Sooner Speaker"));
    }

    void whyTheScheduleIsEmpty()
    {
        auto vm = chapelVm();
        QCOMPARE(vm->scheduleEmptyText(), QStringLiteral("Loading the chapel schedule…"));

        vm->onScheduleFailed(make<TransportError>("timed out"));
        QVERIFY(vm->scheduleEmptyText().startsWith("Couldn't reach the chapel schedule"));

        vm->onScheduleLoaded({});
        QCOMPARE(vm->scheduleEmptyText(), QStringLiteral("No chapels scheduled right now."));

        vm->onScheduleFailed(make<ParseError>("no Items"));
        QCOMPARE(vm->scheduleEmptyText(), QStringLiteral("The chapel schedule feed changed shape."));
    }

    void aPopulatedScheduleHasNoEmptyText() { QCOMPARE(scheduled()->scheduleEmptyText(), QString()); }

    // ---- Dining viewmodel ----------------------------------------------------

    void theMealsQualifierFollowsThePage()
    {
        auto vm = diningVm();
        vm->m_plan.mealsRemaining = 19;
        vm->m_plan.period = "week";
        QCOMPARE(vm->mealsPeriodText(), QStringLiteral("left this week"));
        QCOMPARE(vm->planDescription(), QStringLiteral("Weekly meal plan"));
    }

    void anUnknownCycleDropsTheQualifierRatherThanInventingOne()
    {
        auto vm = diningVm();
        vm->m_plan.mealsRemaining = 19;
        QCOMPARE(vm->mealsPeriodText(), QStringLiteral("left"));
        QCOMPARE(vm->planDescription(), QString());
    }

    void theScanLimitFollowsThePlanKind()
    {
        auto vm = diningVm();
        QCOMPARE(vm->scansPerPeriod(), 0);
        vm->m_plan.period = "week";
        QCOMPARE(vm->scansPerPeriod(), 1);
        vm->m_plan.period = "term";
        QCOMPARE(vm->scansPerPeriod(), 5);
    }

    void unreportedBalancesAreSentinels()
    {
        auto vm = diningVm();
        QCOMPARE(vm->mealsRemaining(), -1);
        QCOMPARE(vm->diningDollars(), QString());
        QCOMPARE(vm->flexDollars(), QString());
        QCOMPARE(vm->hasFlexDollars(), false);
        QCOMPARE(vm->hasPlan(), false);
    }

    // The permanent flex tile is hidden unless there is money in it.
    void permanentFlexIsOnlyShownWhenThereIsSome()
    {
        auto vm = diningVm();
        MealPlan plan;
        plan.mealsRemaining = 16;
        vm->onPlanLoaded(plan);
        QCOMPARE(vm->hasFlexDollars(), false);

        plan.flexDollars = 0.0;
        vm->onPlanLoaded(plan);
        QCOMPARE(vm->hasFlexDollars(), false);

        plan.flexDollars = 0.004; // still "$0.00"
        vm->onPlanLoaded(plan);
        QCOMPARE(vm->hasFlexDollars(), false);

        plan.flexDollars = 25.0;
        vm->onPlanLoaded(plan);
        QCOMPARE(vm->hasFlexDollars(), true);
    }

    // Each sitting's tab carries its own hours, for the day shown.
    void theMealTabsCarryThatDaysServingHours()
    {
        auto vm = diningVm();
        vm->now = [] { return local(2026, 9, 25, 7, 0); }; // a Friday
        QCOMPARE(vm->mealTabs().first().toMap().value("hours").toString(), QStringLiteral("7:00–9:30"));
        QCOMPARE(vm->mealTabs().at(1).toMap().value("hours").toString(), QStringLiteral("10:30–2:30"));
        vm->selectDay(1); // Saturday
        QCOMPARE(vm->mealTabs().first().toMap().value("hours").toString(), QStringLiteral("8:00–9:00"));
    }

    // The card's own header already names the meal; the list must not repeat it.
    void theNextSittingIsExposedWithoutAHeadingRow()
    {
        auto vm = diningVm();
        vm->onLoaded({DayMenu{today(), {block("Breakfast", "breakfast",
                                              {{"Bacon", {}}, {"Biscuits & Country Gravy", {"gluten", "dairy"}}})}}});

        auto *model = static_cast<QAbstractItemModel *>(vm->nextMealItems());
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(cell(model, 0, MenuListModel::TextRole).toString(), QStringLiteral("Bacon"));
        QCOMPARE(cell(model, 1, MenuListModel::AllergenRole).toString(), QStringLiteral("gluten, dairy"));
    }

    // The two are different questions and must not share a model.
    void pagingTheDiningTabDoesNotMoveTheSummaryCard()
    {
        QList<Fetch> fetches;
        auto vm = diningVm(&fetches);
        vm->onLoaded({
            DayMenu{today(), {block("Dinner", "dinner", {{"Bratwurst", {}}})}},
            DayMenu{today().addDays(1), {block("Breakfast", "breakfast", {{"Bacon", {}}})}},
        });
        const QString before = vm->nextMealLabel();

        vm->nextDay();

        QCOMPARE(vm->dayOffset(), 1);
        QCOMPARE(vm->nextMealLabel(), before);
    }

    void noMenuAtAllIsReportedAsNoMenu()
    {
        auto vm = diningVm();
        vm->onLoaded({});
        QCOMPARE(vm->hasNextMeal(), false);
        QCOMPARE(vm->nextMealLabel(), QString());
        QCOMPARE(static_cast<QAbstractItemModel *>(vm->nextMealItems())->rowCount(), 0);
        // Still names the station, so the card has a title while it is empty.
        QCOMPARE(vm->nextMealVenue(), HOME_COOKING);
    }

    // Calling it "up next" would have people turning up to a closed hall.
    void tomorrowsBreakfastSaysSo()
    {
        auto vm = diningVm();
        vm->onLoaded({DayMenu{today().addDays(1), {block("Breakfast", "breakfast", {{"Bacon", {}}})}}});
        QCOMPARE(vm->nextMealWhen(), QStringLiteral("Tomorrow"));
        QCOMPARE(vm->nextMealLabel(), QStringLiteral("Breakfast"));
    }

    void theNextSittingSaysWhenItIsServed()
    {
        auto vm = diningVm();
        vm->now = [] { return local(2026, 9, 16, 9, 0); }; // a Wednesday
        vm->onLoaded(wednesdayMenu());
        QCOMPARE(vm->nextMealLabel(), QStringLiteral("Breakfast"));
        QCOMPARE(vm->nextMealHours(), QStringLiteral("7am–9:30am"));
    }

    void noNextSittingHasNoHours()
    {
        auto vm = diningVm();
        vm->onLoaded({});
        QCOMPARE(vm->nextMealHours(), QString());
    }

    // An app left open through 9:30 must flip to lunch on its own.
    void theCardMovesOnWhenBreakfastClosesWithoutARefresh()
    {
        auto vm = diningVm();
        vm->now = [] { return local(2026, 9, 16, 9, 0); };
        vm->onLoaded(wednesdayMenu());

        // The card's rows are rebuilt only when the sitting moves on: this
        // runs twice a minute.
        QSignalSpy spy(static_cast<QAbstractItemModel *>(vm->nextMealItems()), &QAbstractItemModel::modelReset);

        vm->tick(); // still breakfast: nothing to rebuild
        QCOMPARE(spy.count(), 0);

        vm->now = [] { return local(2026, 9, 16, 9, 31); };
        vm->tick();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(vm->nextMealLabel(), QStringLiteral("Lunch"));
        QCOMPARE(vm->nextMealHours(), QStringLiteral("10:30am–2:30pm"));
        QCOMPARE(static_cast<QAbstractItemModel *>(vm->nextMealItems())->rowCount(), 1);
    }

    // ---- Paging through days (the Chucks tab) --------------------------------

    void pagingBackFetchesTheWeekEndingOnThatDay()
    {
        QList<Fetch> fetches;
        auto vm = diningVm(&fetches);
        vm->onLoaded({home(today(), {"Bratwurst"})});
        QVERIFY(fetches.isEmpty());

        vm->previousDay();

        QCOMPARE(vm->dayOffset(), -1);
        QCOMPARE(vm->dateText(), QStringLiteral("Yesterday"));
        QCOMPARE(vm->dayLoading(), true);
        QCOMPARE(vm->dayEmptyText(), QStringLiteral("Loading the menu…"));
        QCOMPARE(fetches.size(), 1);
        QCOMPARE(fetches[0].provider.start(), today().addDays(-7));
        QCOMPARE(fetches[0].provider.days(), 7);
    }

    void aFetchedWindowFillsTheDayAndServesTheRestOfTheWeek()
    {
        QList<Fetch> fetches;
        auto vm = diningVm(&fetches);
        vm->onLoaded({home(today(), {"Bratwurst"})});
        vm->previousDay();

        fetches[0].done({home(today().addDays(-1), {"Tacos"}), home(today().addDays(-2), {"Lasagna"})}, {});

        QCOMPARE(texts(*vm), (QStringList{"Tacos"}));
        QCOMPARE(vm->dayLoading(), false);
        vm->previousDay();
        QCOMPARE(texts(*vm), (QStringList{"Lasagna"}));
        QCOMPARE(fetches.size(), 1);
    }

    void pagingForwardPastTheWeekFetchesFromThatDay()
    {
        QList<Fetch> fetches;
        auto vm = diningVm(&fetches);
        vm->onLoaded({home(today(), {"Bratwurst"})});
        for (int i = 0; i < 7; ++i)
            vm->nextDay();

        QCOMPARE(fetches.size(), 1);
        QCOMPARE(fetches[0].provider.start(), today().addDays(7));
    }

    void aDayWithNothingPostedSaysSo()
    {
        QList<Fetch> fetches;
        auto vm = diningVm(&fetches);
        vm->onLoaded({DayMenu{today(), {block("", "anytime", {}, "No Venues Found")}}});
        QCOMPARE(static_cast<QAbstractItemModel *>(vm->stations())->rowCount(), 0);
        QCOMPARE(vm->dayEmptyText(), QStringLiteral("Nothing posted for this day."));
        QVERIFY(fetches.isEmpty());
    }

    void aFailedWindowIsReportedOnItsOwnDayOnly()
    {
        QList<Fetch> fetches;
        auto vm = diningVm(&fetches);
        vm->onLoaded({home(today(), {"Bratwurst"})});
        vm->previousDay();

        fetches[0].failed(make<TransportError>("timed out"));

        QVERIFY(vm->dayEmptyText().contains("Couldn't reach the dining menu service"));
        QCOMPARE(vm->error(), QString()); // the summary card's menu is unaffected

        vm->goToToday();
        QCOMPARE(vm->isToday(), true);
        QCOMPARE(texts(*vm), (QStringList{"Bratwurst"}));
        QCOMPARE(vm->dayEmptyText(), QString());
    }

    void goingBackToAFailedDayTriesAgain()
    {
        QList<Fetch> fetches;
        auto vm = diningVm(&fetches);
        vm->onLoaded({home(today(), {"Bratwurst"})});
        vm->previousDay();
        fetches[0].failed(make<TransportError>("timed out"));

        vm->nextDay();
        vm->previousDay();

        QCOMPARE(fetches.size(), 2);
        QCOMPARE(vm->dayEmptyText(), QStringLiteral("Loading the menu…"));
    }

    void aRefreshRefetchesAPagedDayRatherThanKeepingIt()
    {
        QList<Fetch> fetches;
        auto vm = diningVm(&fetches);
        vm->onLoaded({home(today(), {"Bratwurst"})});
        vm->previousDay();
        fetches[0].done({home(today().addDays(-1), {"Tacos"})}, {});

        vm->onLoaded({home(today(), {"Bratwurst"})});

        QCOMPARE(fetches.size(), 2);
        QCOMPARE(fetches[1].provider.start(), today().addDays(-1));
    }

    void aDistantDayNamesItsYear()
    {
        DiningViewModel vm(nullptr);
        const QDate target(today().year() - 1, 9, 1);
        vm.m_offset = static_cast<int>(today().daysTo(target));
        QVERIFY(vm.dateDetail().endsWith(QStringLiteral(", %1").arg(today().year() - 1)));
        QVERIFY(vm.dateDetail().startsWith(QLocale::c().toString(target, "dddd") + ", September 1"));
    }

    void aNearbyDayIsAShortDate()
    {
        DiningViewModel vm(nullptr);
        vm.m_offset = 3;
        const QDate target = today().addDays(3);
        QCOMPARE(vm.dateText(), fmt::dayShort(target) + " " + fmt::monthShort(target) + " "
                                    + QString::number(target.day()));
    }

    // ---- Recent activity (the Dining tab) -----------------------------------

    void activityIsGroupedByDayUnderReadableHeaders()
    {
        auto vm = diningVm();
        MealPlan plan;
        plan.mealsRemaining = 16;
        plan.transactions = activityFixture();
        vm->onPlanLoaded(plan);
        const QDate threeDaysAgo = today().addDays(-3);

        // A day's header sums it.
        QCOMPARE(activityRows(*vm), (QList<QVariantList>{
                                        {true, "Today", "1 meal · $3.74", ""},
                                        {false, "Board meal", "Lunch · 12:25 PM", ""},
                                        {false, "Flex purchase", "12:05 AM", "−$3.74"},
                                        {true, "Yesterday", "1 meal · $6.00", ""},
                                        {false, "Meal exchange", "Dinner · 5:45 PM", ""},
                                        {false, "Flex purchase", "Lunch · 12:00 PM", "−$6.00"},
                                        {true, fmt::shortDate(threeDaysAgo), "1 meal", ""},
                                        {false, "Board meal", "Breakfast · 7:40 AM", ""},
                                    }));
        QCOMPARE(vm->activitySummary(),
                 "Since " + fmt::monthDay(threeDaysAgo) + " · 3 meals · $9.74 flex spent");
        QCOMPARE(vm->activityEmptyText(), QString());
    }

    void theFlexToggleShowsOnlyMoney()
    {
        auto vm = diningVm();
        MealPlan plan;
        plan.mealsRemaining = 16;
        plan.transactions = activityFixture();
        vm->onPlanLoaded(plan);

        vm->setActivityFilter(2);

        QCOMPARE(vm->activityFilter(), 2);
        QStringList titles;
        for (const auto &row : activityRows(*vm)) {
            if (!row[0].toBool())
                titles.append(row[1].toString());
        }
        QCOMPARE(titles, (QStringList{"Flex purchase", "Flex purchase"}));
        QVERIFY(vm->activitySummary().endsWith(" · 2 purchases · $9.74"));

        vm->setActivityFilter(1);
        QVERIFY(vm->activitySummary().endsWith(" · 3 meals"));

        vm->setActivityFilter(0);
        int dishes = 0;
        for (const auto &row : activityRows(*vm))
            dishes += row[0].toBool() ? 0 : 1;
        QCOMPARE(dishes, activityFixture().size());
    }

    void aDepositIsSummedSeparatelyFromSpending()
    {
        auto vm = diningVm();
        MealPlan plan;
        plan.transactions = {txn(atDaysAgo(0, 9), "Deposit", {}, 20.0, true),
                             txn(atDaysAgo(0, 8), "Flex purchase", {}, 3.0)};
        vm->onPlanLoaded(plan);
        vm->setActivityFilter(2);
        QVERIFY(vm->activitySummary().endsWith(" · 1 purchase · $3.00 · +$20.00 added"));
    }

    void whyTheActivityListIsEmpty()
    {
        auto vm = diningVm();
        QCOMPARE(vm->activityEmptyText(), QStringLiteral("Sign in to see your meal plan activity."));
        QCOMPARE(vm->activitySummary(), QString());

        MealPlan noActivity;
        noActivity.mealsRemaining = 16;
        vm->onPlanLoaded(noActivity);
        QCOMPARE(vm->activityEmptyText(), QStringLiteral("No recent activity on this card."));

        MealPlan swipesOnly;
        swipesOnly.transactions = activityFixture().mid(0, 1);
        vm->onPlanLoaded(swipesOnly);
        vm->setActivityFilter(2);
        QCOMPARE(static_cast<QAbstractItemModel *>(vm->activity())->rowCount(), 0);
        QCOMPARE(vm->activityEmptyText(), QStringLiteral("No flex purchases in your recent activity."));
    }

    void theFixtureActivityLoadsEndToEnd()
    {
        auto vm = diningVm();
        vm->onPlanLoaded(MealsProvider(std::make_shared<FixtureTransport>(testing::fixturesDir())).fetch());
        QVERIFY(static_cast<QAbstractItemModel *>(vm->activity())->rowCount() > 19); // every row, plus a header per day
        QCOMPARE(vm->activitySummary(), QStringLiteral("Since Dec 27 · 16 meals · $13.99 flex spent"));
    }

    // ---- Curfew (the Buildings tab) -----------------------------------------

    void aWeeknightCurfew()
    {
        auto vm = curfewAt(local(2026, 9, 29, 20, 0)); // Tuesday
        QCOMPARE(vm->timeText(), QStringLiteral("11:59 PM"));
        QCOMPARE(vm->nightText(), QStringLiteral("Tuesday night"));
        QVERIFY(!vm->lateNight());
    }

    // At 12:30 AM on Saturday it is still Friday night, with half an hour left.
    void fridayNightRunsPastMidnight()
    {
        auto vm = curfewAt(local(2026, 10, 3, 0, 29));
        QCOMPARE(vm->timeText(), QStringLiteral("12:59 AM"));
        QCOMPARE(vm->nightText(), QStringLiteral("Friday night"));
        QVERIFY(vm->lateNight());
        QCOMPARE(vm->countdownText(), QStringLiteral("30:00"));
    }

    void theCountdownIsOnlyForTheLastHour()
    {
        QCOMPARE(curfewAt(local(2026, 9, 29, 22, 58))->countdownText(), QString());
        QCOMPARE(curfewAt(local(2026, 9, 29, 22, 59))->countdownText(), QStringLiteral("60:00"));
        QCOMPARE(curfewAt(QDateTime(QDate(2026, 9, 29), QTime(23, 58, 55)))->countdownText(),
                 QStringLiteral("0:05"));
        // Past curfew it moves on to tomorrow night, with no countdown yet.
        auto after = curfewAt(local(2026, 9, 29, 23, 59));
        QCOMPARE(after->nightText(), QStringLiteral("Wednesday night"));
        QCOMPARE(after->countdownText(), QString());
    }

    void refreshingReReadsTheClock()
    {
        auto vm = curfewAt(local(2026, 9, 29, 22, 0));
        QSignalSpy changed(vm.get(), &CurfewViewModel::changed);
        vm->refreshAll();
        QCOMPARE(changed.count(), 0);
        vm->now = [] { return local(2026, 9, 29, 23, 30); };
        vm->refreshAll();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(vm->countdownText(), QStringLiteral("29:00"));
    }

    // ---- Cache-first ----------------------------------------------------------

    // What the last run saved is on screen before any request, stamped as such.
    void theChapelScreenOpensOnWhatWasSaved()
    {
        const QString dir = freshDir();
        const Storage storage = Storage::at(dir);
        auto transport = std::make_shared<FixtureTransport>(testing::fixturesDir());
        storage.cache.save(cachekey::CHAPEL, ChapelProvider(transport).fetchPayload(),
                           QDateTime::currentDateTime().addSecs(-3600));
        storage.cache.save(cachekey::SCHEDULE, fetchSchedulePayload(transport));
        storage.session.update([](SessionState &state) { state.studentId = "1234567"; });

        ChapelViewModel vm(transport, storage);

        QCOMPARE(vm.remaining(), 16);
        QVERIFY(vm.loaded());
        QVERIFY(vm.skipsStatus()->hasData());
        QVERIFY(vm.skipsStatus()->fromCache());
        QVERIFY(vm.skipsStatus()->stale());
        QVERIFY(vm.skipsStatus()->updatedText().startsWith("Updated "));
        QVERIFY(vm.scheduleStatus()->hasData());
        // The remembered ID goes to the provider, so the refresh skips the
        // dashboard.
        QCOMPARE(vm.m_provider->knownStudentId(), QStringLiteral("1234567"));

        // A refresh replaces it, and it is no longer "from the cache".
        vm.onPayloadLoaded(ChapelProvider(transport).fetchPayload());
        QVERIFY(!vm.skipsStatus()->fromCache());
        QVERIFY(!vm.skipsStatus()->stale());
    }

    void aRefreshIsSavedForNextTime()
    {
        const QString dir = freshDir();
        auto transport = std::make_shared<FixtureTransport>(testing::fixturesDir());
        {
            ChapelViewModel vm(transport, Storage::at(dir));
            vm.onPayloadLoaded(ChapelProvider(transport).fetchPayload());
        }
        ChapelViewModel next(transport, Storage::at(dir));
        QCOMPARE(next.remaining(), 16);
        QVERIFY(next.skipsStatus()->fromCache());
    }

    // A saved copy the parsers no longer accept is not shown, and not fatal.
    void anUnreadableSavedCopyIsIgnored()
    {
        const QString dir = freshDir();
        Storage::at(dir).cache.save(cachekey::CHAPEL, QJsonObject{{"summary", QJsonArray{1, 2}}});
        ChapelViewModel vm(std::make_shared<FixtureTransport>(testing::fixturesDir()), Storage::at(dir));
        QVERIFY(!vm.loaded());
        QCOMPARE(vm.remaining(), -1);
    }

    void theDiningScreensOpenOnWhatWasSaved()
    {
        const QString dir = freshDir();
        const Storage storage = Storage::at(dir);
        auto transport = std::make_shared<FixtureTransport>(testing::fixturesDir());
        storage.cache.save(cachekey::MENUS, DiningProvider(transport).fetchPayload());
        storage.cache.save(cachekey::MEALS, MealsProvider(transport).fetchPayload());

        DiningViewModel vm(transport, storage);

        QVERIFY(vm.menuStatus()->hasData());
        QVERIFY(vm.menuStatus()->fromCache());
        QVERIFY(vm.hasMenuFor(QDate(2026, 9, 16)));
        QCOMPARE(vm.mealsRemaining(), 16);
        QCOMPARE(vm.diningDollars(), QStringLiteral("$102.34"));
        QVERIFY(vm.planStatus()->fromCache());
    }

    void thePlanRemembersItsTarget()
    {
        const QString dir = freshDir();
        auto transport = std::make_shared<FixtureTransport>(testing::fixturesDir());
        DiningViewModel vm(transport, Storage::at(dir));
        vm.m_mealsProvider->fetchPayload(); // reads the target off the page
        vm.onPlanPayloadLoaded(MealsProvider(transport).fetchPayload());
        QCOMPARE(SessionStore(dir).load().mealsPersonId, QStringLiteral("0000000"));
    }

    // ---- The meal plan's failures ---------------------------------------------

    void anExpiredSessionOnThePlanIsRoutedToSignIn()
    {
        auto vm = diningVm();
        QSignalSpy spy(vm.get(), &DiningViewModel::sessionExpired);
        vm->m_planBusy = true;
        vm->onPlanFailed(make<SessionExpired>("gone"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(vm->planStatus()->error(), QString());
        QCOMPARE(vm->planStatus()->loading(), false);
        QCOMPARE(vm->m_planBusy, false);
    }

    void planFailuresAreTranslated()
    {
        auto vm = diningVm();
        vm->onPlanFailed(make<TransportError>("could not reach"));
        QVERIFY(vm->planStatus()->error().startsWith("Couldn't reach Self-Service"));
        QVERIFY(vm->planStatus()->failedOffline());

        vm->onPlanFailed(std::make_exception_ptr(TransportError("HTTP 500", 500)));
        QVERIFY(!vm->planStatus()->failedOffline());

        // The server's own words, when it says the meal plan system is down.
        vm->onPlanFailed(make<ParseError>("Self-Service could not load the meal plan: try later"));
        QCOMPARE(vm->planStatus()->error(), QStringLiteral("Self-Service could not load the meal plan: try later"));
    }

    // A failed refresh never blanks what was there.
    void aFailedRefreshKeepsTheFigures()
    {
        auto vm = diningVm();
        MealPlan plan;
        plan.mealsRemaining = 16;
        vm->onPlanLoaded(plan);
        vm->onPlanFailed(make<TransportError>("timed out"));
        QCOMPARE(vm->mealsRemaining(), 16);
        QVERIFY(vm->planStatus()->hasData());
        QVERIFY(!vm->planStatus()->error().isEmpty());
    }

    void refreshPlanIsNotReEntrant()
    {
        auto vm = diningVm();
        vm->m_planBusy = true;
        QSignalSpy spy(vm.get(), &DiningViewModel::changed);
        vm->refreshPlan();
        QCOMPARE(spy.count(), 0);
    }

    // ---- Sign-out --------------------------------------------------------------

    void clearPersonalDropsTheRecordsAndKeepsThePublicData()
    {
        auto chapelModel = chapelVm();
        chapelModel->onLoaded(figures(2, 18, 16));
        chapelModel->onScheduleLoaded({chapel(QDateTime::currentDateTime().addDays(1), "Worship Chapel")});
        chapelModel->clearPersonal();
        QCOMPARE(chapelModel->remaining(), -1);
        QVERIFY(!chapelModel->loaded());
        QVERIFY(chapelModel->hasNextChapel());

        auto dining = diningVm();
        dining->onLoaded({home(today(), {"Bacon"})});
        MealPlan plan;
        plan.mealsRemaining = 16;
        plan.transactions = activityFixture();
        dining->onPlanLoaded(plan);
        dining->clearPersonal();
        QCOMPARE(dining->mealsRemaining(), -1);
        QVERIFY(!dining->planStatus()->hasData());
        QCOMPARE(static_cast<QAbstractItemModel *>(dining->activity())->rowCount(), 0);
        QVERIFY(dining->menuStatus()->hasData());
        QCOMPARE(texts(*dining), QStringList{"Bacon"});
    }

    // ---- The Menu section ------------------------------------------------------

    void theFeaturedStationsAreListedWithTheBarGathered()
    {
        auto vm = lunchVm();
        QCOMPARE(stationRows(*vm), (QList<QVariantList>{
                                       {"header", HOME_COOKING, "", "", false},
                                       {"item", HOME_COOKING, "Beef Ragu", "", false},
                                       {"item", HOME_COOKING, "Alfredo Sauce", "", false},
                                       {"item", HOME_COOKING, "Garlic Breadsticks", "", true},
                                       {"header", GARDEN_BITES, "", "", false},
                                       {"item", GARDEN_BITES, "Broccoli Alfredo", "", false},
                                       {"extras", GARDEN_BITES, "Potato bar: Whipped Butter, Baked Potatoes", "", true},
                                       {"header", ALLERGEN_AWARE, "", "", false},
                                       {"item", ALLERGEN_AWARE, "Chicken Cacciatore", "", true},
                                   }));
        auto *model = static_cast<QAbstractItemModel *>(vm->stations());
        QCOMPARE(cell(model, 3, StationListModel::NewRole).toBool(), true);
        QCOMPARE(cell(model, 2, StationListModel::AllergenRole).toString(), QStringLiteral("gluten, dairy"));
        // An all-day station is not one of the three.
        QCOMPARE(vm->allDayStations().size(), 1);
        QCOMPARE(vm->allDayStations().first().toMap().value("name").toString(), QStringLiteral("Italian"));
    }

    void avoidingAnAllergenHidesWhatHasIt()
    {
        auto vm = lunchVm();
        vm->toggleAvoid("dairy");

        QCOMPARE(vm->hiddenCount(), 3);
        QCOMPARE(vm->hiddenText(), QStringLiteral("Hiding 3 items with dairy"));
        QCOMPARE(texts(*vm), (QStringList{"Beef Ragu", "Garlic Breadsticks", "Potato bar: Baked Potatoes",
                                          "Chicken Cacciatore"}));
        QCOMPARE(stationRows(*vm).at(3).at(3).toString(), QStringLiteral("2 hidden")); // Garden Bites

        // Everything at a station hidden: it says so rather than vanishing.
        vm->toggleAvoid("gluten");
        vm->toggleAvoid("soy");
        QVERIFY(texts(*vm).contains("Everything here has something you're avoiding."));

        vm->clearAvoid();
        QCOMPARE(vm->hiddenCount(), 0);
        QCOMPARE(vm->hiddenText(), QString());
    }

    // 9:42 on a Thursday: breakfast is over, so the menu opens on lunch.
    void theMenuOpensOnTheSittingTheClockSays()
    {
        auto vm = lunchVm();
        vm->m_mealPinned = false;
        vm->tick();
        QCOMPARE(vm->selectedMeal(), QStringLiteral("lunch"));
        QCOMPARE(vm->mealStatusText(), QStringLiteral("Opens in 48 min · 10:30 AM – 2:30 PM"));
        QVERIFY(vm->mealStatusLive());

        vm->selectMeal("breakfast");
        QCOMPARE(vm->mealStatusText(), QStringLiteral("Ended at 9:30 AM"));
        vm->selectMeal("dinner");
        QCOMPARE(vm->mealStatusText(), QStringLiteral("Tonight · 4:30 PM – 7:30 PM"));
        QVERIFY(!vm->mealStatusLive());
        vm->selectDay(1);
        QCOMPARE(vm->mealStatusText(), QStringLiteral("Tomorrow · 4:30 PM – 7:30 PM"));
    }

    void theDayStripIsAWeekAroundToday()
    {
        auto vm = lunchVm();
        const QVariantList days = vm->days();
        QCOMPARE(days.size(), 7);
        QCOMPARE(days.at(3).toMap().value("isToday").toBool(), true);
        QCOMPARE(days.at(3).toMap().value("dow").toString(), QStringLiteral("Thu"));
        QCOMPARE(days.at(3).toMap().value("selected").toBool(), true);
        // Fetched with nothing on it: not available. Never asked for: available.
        QCOMPARE(days.at(4).toMap().value("available").toBool(), false);
        QCOMPARE(days.at(0).toMap().value("available").toBool(), true);
    }

    // ---- The meal plan's figures -------------------------------------------------

    // Thursday Sep 17: $102.34 left after $9.74 since Monday, 89 days to Dec 11.
    void theFlexPaceSpreadsMondaysBalanceOverTheTerm()
    {
        auto vm = diningVm();
        vm->now = [] { return local(2026, 9, 17, 12, 0); };
        MealPlan plan;
        plan.diningDollars = 102.34;
        plan.planName = "21 Meals";
        plan.period = "week";
        plan.transactions = {txn(local(2026, 9, 16, 19, 30), "Flex purchase", "Dinner", 3.74),
                             txn(local(2026, 9, 14, 15, 20), "Flex purchase", "Lunch", 6.00),
                             txn(local(2026, 9, 12, 12, 0), "Flex purchase", "Lunch", 50.00)};
        vm->onPlanLoaded(plan);

        QVERIFY(vm->hasPace());
        QCOMPARE(vm->flexPerWeekText(), QStringLiteral("$8.82 a week"));
        QCOMPARE(vm->paceEndText(), QStringLiteral("Dec 11"));
        QCOMPARE(vm->spentThisWeek(), QStringLiteral("$9.74"));
        QVERIFY(vm->overPace());
        QCOMPARE(vm->paceDeltaText(), QStringLiteral("$0.92 over pace"));
        QCOMPARE(vm->paceSpentFraction(), 1.0);
        QVERIFY(qAbs(vm->paceBudgetFraction() - (112.08 * 7 / 89) / 9.74) < 1e-9);
        QCOMPARE(vm->mealsPerPeriod(), 21);
        QCOMPARE(vm->planTitle(), QStringLiteral("21-meal plan"));
    }

    void thereIsNoPaceOutsideATermOrWithoutABalance()
    {
        auto vm = diningVm();
        vm->now = [] { return local(2026, 7, 1, 12, 0); };
        MealPlan plan;
        plan.diningDollars = 50.0;
        vm->onPlanLoaded(plan);
        QVERIFY(!vm->hasPace());
        QCOMPARE(vm->flexPerWeekText(), QString());
        vm->now = [] { return local(2026, 9, 17, 12, 0); };
        vm->onPlanLoaded(MealPlan());
        QVERIFY(!vm->hasPace());
    }

    void exchangesNameWhereTheyWorkToday()
    {
        auto vm = diningVm();
        vm->now = [] { return local(2026, 9, 17, 12, 0); };
        QCOMPARE(vm->exchangeText(), QStringLiteral("Chick-fil-A, Panda Express, The Cafe until 8 PM"));
        vm->now = [] { return local(2026, 9, 20, 12, 0); }; // Sunday
        QCOMPARE(vm->exchangeText(), QStringLiteral("The Cafe until 8 PM"));
    }

    // ---- The Chapel screen's figures ---------------------------------------------

    void consecutiveChapelsBySpeakerArePartsOfASeries()
    {
        const QStringList parts = seriesParts({
            chapel(local(2026, 9, 21), "Dr. Thomas White", {"Dr. Thomas White"}),
            chapel(local(2026, 9, 22), "Garrett Kell", {"Garrett Kell"}),
            chapel(local(2026, 9, 23), "Garrett Kell", {"Garrett Kell"}),
            chapel(local(2026, 9, 25), "Philip Miller", {"Philip Miller"}),
            chapel(local(2026, 9, 28), "Philip Miller", {"Philip Miller"}), // Fri, then Mon
            chapel(local(2026, 10, 6), "Worship Chapel"),
            chapel(local(2026, 10, 7), "Worship Chapel"),
            chapel(local(2026, 10, 12), "Dr. Thomas White", {"Dr. Thomas White"}),
        });
        QCOMPARE(parts, (QStringList{"", "Part 1 of 2", "Part 2 of 2", "Part 1 of 2", "Part 2 of 2", "", "", ""}));
    }

    void theRequirementReasonsFitOnChips()
    {
        auto vm = chapelVm();
        ChapelSummary s = figures(2, 18, 16);
        s.requirementReasons = {"Not a Distance Learner", "Registered for 15.5 credits (more than 6)",
                                "Undergraduate Student"};
        s.allowance = {{"Skips Allowed", 17, "Base semester allowance"}, {"Manual Arrangement", 1, {}}};
        vm->onLoaded(s);
        QCOMPARE(vm->requirementReasons(), (QStringList{"Not a distance learner", "15.5 credits", "Undergraduate"}));
        const QVariantList allowance = vm->allowance();
        QCOMPARE(allowance.size(), 2);
        QCOMPARE(allowance.at(0).toMap().value("figure").toString(), QStringLiteral("17"));
        QCOMPARE(allowance.at(0).toMap().value("label").toString(), QStringLiteral("Base allowance"));
        QCOMPARE(allowance.at(1).toMap().value("figure").toString(), QStringLiteral("+1"));
        QCOMPARE(allowance.at(1).toMap().value("label").toString(), QStringLiteral("Manual arrangement"));
    }

    // 16 left with 85 days to Dec 11.
    void theSkipsLeftAreSpreadOverTheTerm()
    {
        auto vm = chapelVm();
        vm->now = [] { return local(2026, 9, 17, 9, 42); };
        vm->onLoaded(figures(2, 18, 16));
        QCOMPARE(vm->skipsPerWeekText(), QStringLiteral("About 1 a week through Dec 11"));
        vm->onLoaded(figures(13, 18, 5));
        QCOMPARE(vm->skipsPerWeekText(), QStringLiteral("About 1 every 2 weeks through Dec 11"));
        vm->onLoaded(figures(18, 18, 0));
        QCOMPARE(vm->skipsPerWeekText(), QStringLiteral("None to spare through Dec 11"));
        vm->now = [] { return local(2026, 7, 1, 9, 0); };
        QCOMPARE(vm->skipsPerWeekText(), QString());
    }

    void aLedgerRowReadsAsWhatHappenedAndWhen()
    {
        ChapelListModel model;
        model.replace({entry(local(2026, 8, 20), 1, "Chapel Skip", "Absent from Chapel 8/20/2026")});
        QCOMPARE(cell(&model, 0, ChapelListModel::TitleRole).toString(), QStringLiteral("Absent from Chapel"));
        QCOMPARE(cell(&model, 0, ChapelListModel::DetailRole).toString(), QStringLiteral("Thu, Aug 20 · chapel skip"));
    }

    void todaysChapelCountsDown()
    {
        auto vm = chapelVm();
        vm->now = [] { return local(2026, 9, 17, 9, 42); };
        vm->onScheduleLoaded({chapel(local(2026, 9, 17), "Garrett Higbee", {"Garrett Higbee"}, {}, true),
                              chapel(local(2026, 9, 18), "Worship Chapel")});
        QVERIFY(vm->chapelToday());
        QCOMPARE(vm->nextChapelCountdown(), QStringLiteral("in 18 min"));
        QCOMPARE(vm->fromNowText(local(2026, 9, 17)), QStringLiteral("18 min"));
        QCOMPARE(scheduleRows(*vm, {"badge"}).at(1).first().toString(), QStringLiteral("Today · in 18 min"));
        QCOMPARE(vm->watchUrl("ySIBhMll7k0"), QStringLiteral("https://www.youtube.com/watch?v=ySIBhMll7k0"));

        vm->now = [] { return local(2026, 9, 17, 10, 10); };
        QCOMPARE(vm->fromNowText(local(2026, 9, 17)), QStringLiteral("Now"));
    }

private:
    static std::unique_ptr<CurfewViewModel> curfewAt(QDateTime when)
    {
        auto vm = std::make_unique<CurfewViewModel>();
        vm->now = [when] { return when; };
        vm->refreshAll();
        return vm;
    }

    // A Wednesday, ten minutes into chapel.
    std::unique_ptr<ChapelViewModel> scheduled()
    {
        auto vm = chapelVm();
        vm->now = [] { return local(2026, 9, 23, 10, 10); };
        vm->onScheduleLoaded({
            chapel(local(2026, 9, 22), "Garrett Kell", {"Garrett Kell"}),
            chapel(local(2026, 9, 23), "Garrett Kell", {"Garrett Kell"}, "Lead pastor of Del Ray.", true),
            chapel(local(2026, 9, 24), "SGA", {}, {}, true),
            chapel(local(2026, 9, 28), "Sermon on the Mount", {"Philip Miller"}, {}, true),
            chapel(local(2026, 10, 5, 11), "Majors Assembly"),
            chapel({}, "Undated"),
        });
        return vm;
    }

    // Thursday Sep 17 at 9:42, the canvas's morning: three featured stations at
    // lunch, and an all-day one.
    std::unique_ptr<DiningViewModel> lunchVm()
    {
        auto vm = diningVm();
        vm->now = [] { return local(2026, 9, 17, 9, 42); };
        vm->onLoaded({DayMenu{
            QDate(2026, 9, 17),
            {block("Lunch", "lunch",
                   {{"Beef Ragu", {}}, {"Alfredo Sauce", {"gluten", "dairy"}}, {"Garlic Breadsticks", {"gluten"}, true}}),
             block("Lunch", "lunch",
                   {{"Broccoli Alfredo", {"dairy"}}, {"Whipped Butter", {"dairy"}}, {"Baked Potatoes", {}}},
                   GARDEN_BITES),
             block("Lunch", "lunch", {{"Chicken Cacciatore", {"soy"}}}, ALLERGEN_AWARE),
             block("", "anytime", {{"Pepperoni Pizza", {"gluten", "dairy"}}}, "Italian")}}});
        // Tomorrow fetched, with nothing posted.
        vm->store({QDate(2026, 9, 18)}, {});
        vm->selectMeal("lunch");
        return vm;
    }

    static QList<DayMenu> wednesdayMenu()
    {
        return {DayMenu{QDate(2026, 9, 16),
                        {block("Breakfast", "breakfast", {{"Bacon", {}}}),
                         block("Lunch", "lunch", {{"Pork Loin", {}}})}}};
    }
};

} // namespace mycu

CEDARVIEW_TEST_MAIN(mycu::TestViewModels, QCoreApplication)
#include "tst_viewmodels.moc"
