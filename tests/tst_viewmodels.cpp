// Viewmodel behaviour, headless.
//
// These touch Qt (offscreen) but never run a worker thread: the completion
// handlers are invoked directly, which is what makes the tests deterministic.
// The threading itself is ui/tasks.h and has its own suite.

#include "testsupport.h"

#include "core/providers/meals.h"
#include "ui/viewmodels/chapel.h"
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

DayMenu home(QDate on, const QStringList &dishes)
{
    QList<MenuItem> items;
    for (const QString &d : dishes)
        items.append(MenuItem{d, {}});
    return DayMenu{on, {block("Lunch", "lunch", items)}};
}

QVariant cell(QAbstractItemModel *model, int row, int role)
{
    return model->data(model->index(row, 0), role);
}

QStringList texts(DiningViewModel &vm)
{
    QStringList out;
    auto *model = static_cast<QAbstractItemModel *>(vm.items());
    for (int r = 0; r < model->rowCount(); ++r)
        out.append(cell(model, r, MenuListModel::TextRole).toString());
    return out;
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

    std::unique_ptr<ChapelViewModel> chapelVm()
    {
        return std::make_unique<ChapelViewModel>(std::make_shared<FixtureTransport>(testing::fixturesDir()),
                                                 SessionStore(m_dir.path() + "/" + QUuid::createUuid().toString(QUuid::Id128)));
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
        QCOMPARE(names, (QSet<QByteArray>{"whenText", "reason", "entryType", "count", "isSkip"}));
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
        ChapelViewModel first(std::make_shared<FixtureTransport>(testing::fixturesDir()), SessionStore(dir));
        ChapelSummary s;
        s.termName = "Fall Semester 2026";
        first.onLoaded(s);

        ChapelViewModel second(std::make_shared<FixtureTransport>(testing::fixturesDir()), SessionStore(dir));
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

    void unreportedBalancesAreSentinels()
    {
        auto vm = diningVm();
        QCOMPARE(vm->mealsRemaining(), -1);
        QCOMPARE(vm->diningDollars(), QString());
        QCOMPARE(vm->flexDollars(), QString());
        QCOMPARE(vm->hasPlan(), false);
    }

    // The card's own header already names the meal; the list must not repeat it.
    void theNextSittingIsExposedWithoutAHeadingRow()
    {
        auto vm = diningVm();
        vm->onLoaded({DayMenu{today(), {block("Breakfast", "breakfast",
                                              {{"Bacon", {}}, {"Biscuits & Country Gravy", {"gluten", "dairy"}}})}}});

        auto *model = static_cast<QAbstractItemModel *>(vm->nextMealItems());
        QCOMPARE(model->rowCount(), 2);
        for (int r = 0; r < model->rowCount(); ++r)
            QCOMPARE(cell(model, r, MenuListModel::HeaderRole).toBool(), false);
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

        QSignalSpy spy(vm.get(), &DiningViewModel::changed);

        vm->tick(); // still breakfast: nothing to say
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

        fetches[0].done({home(today().addDays(-1), {"Tacos"}), home(today().addDays(-2), {"Lasagna"})});

        QCOMPARE(texts(*vm), (QStringList{"Lunch", "Tacos"}));
        QCOMPARE(vm->dayLoading(), false);
        vm->previousDay();
        QCOMPARE(texts(*vm), (QStringList{"Lunch", "Lasagna"}));
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
        QCOMPARE(static_cast<QAbstractItemModel *>(vm->items())->rowCount(), 0);
        QCOMPARE(vm->dayEmptyText(), QStringLiteral("Nothing posted for Home Cooking on this day."));
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
        QCOMPARE(texts(*vm), (QStringList{"Lunch", "Bratwurst"}));
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
        fetches[0].done({home(today().addDays(-1), {"Tacos"})});

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

        QCOMPARE(activityRows(*vm), (QList<QVariantList>{
                                        {true, "Today", "", ""},
                                        {false, "Board meal", "Lunch · 12:25 PM", ""},
                                        {false, "Flex purchase", "12:05 AM", "−$3.74"},
                                        {true, "Yesterday", "", ""},
                                        {false, "Meal exchange", "Dinner · 5:45 PM", ""},
                                        {false, "Flex purchase", "Lunch · 12:00 PM", "−$6.00"},
                                        {true, fmt::shortDate(threeDaysAgo), "", ""},
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

        vm->setFlexOnly(true);

        QCOMPARE(vm->flexOnly(), true);
        QStringList titles;
        for (const auto &row : activityRows(*vm)) {
            if (!row[0].toBool())
                titles.append(row[1].toString());
        }
        QCOMPARE(titles, (QStringList{"Flex purchase", "Flex purchase"}));
        QVERIFY(vm->activitySummary().endsWith(" · 2 purchases · $9.74"));

        vm->setFlexOnly(false);
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
        vm->setFlexOnly(true);
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
        vm->setFlexOnly(true);
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

private:
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
