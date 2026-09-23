// Dining menus, against a REAL captured response.
//
// `tests/fixtures/diningdata_cedarville_edu_api_menus.json` is a verbatim
// capture of `https://diningdata.cedarville.edu/api/menus?days=2` taken on
// 2026-09-16, so a failure here means either the code broke or Cedarville
// changed the API — not that a placeholder needs updating.
//
// The fixture contains no personal data. The endpoint is unauthenticated: the
// dining site's own script fetches it with `credentials: "omit"`.

#include "testsupport.h"

#include "core/providers/dining.h"

using namespace mycu;

namespace {

const QDate FIXTURE_DATE(2026, 9, 16);

QList<DayMenu> menus()
{
    return parseMenus(testing::fixtureJson("diningdata_cedarville_edu_api_menus.json"));
}

QJsonValue parse(const char *text)
{
    const QJsonDocument doc = QJsonDocument::fromJson(text);
    return doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
}

MenuBlock block(const QString &venue, const QString &meal, const QString &slot,
                const QStringList &dishes = {})
{
    MenuBlock b{venue, meal, slot, {}};
    for (const QString &dish : dishes)
        b.items.append(MenuItem{dish, {}});
    return b;
}

QStringList meals(const QList<MenuBlock> &blocks)
{
    QStringList out;
    for (const auto &b : blocks)
        out.append(b.meal);
    return out;
}

// A copy, not a pointer: callers pass temporaries.
std::optional<MenuItem> findItem(const QList<MenuBlock> &blocks, const QString &name)
{
    for (const auto &b : blocks) {
        for (const auto &i : b.items) {
            if (i.name == name)
                return i;
        }
    }
    return std::nullopt;
}

DayMenu fullDay(QDate on)
{
    DayMenu day{on, {}};
    for (const QString &slot : SLOT_ORDER) {
        QString meal = slot;
        meal[0] = meal[0].toUpper();
        day.blocks.append(block(HOME_COOKING, meal, slot, {slot + " dish"}));
    }
    return day;
}

QDateTime at(QDate day, int h, int m) { return QDateTime(day, QTime(h, m)); }

QString nextSlot(const QList<DayMenu> &days, const QDateTime &now)
{
    const auto found = nextMealBlock(days, now);
    return found ? found->second.slot : QStringLiteral("<none>");
}

} // namespace

class TestDiningProvider : public QObject
{
    Q_OBJECT

private slots:
    // ---- Shape ---------------------------------------------------------------

    void theCaptureCoversTwoDays()
    {
        const auto days = menus();
        QCOMPARE(days.size(), 2);
        QCOMPARE(days[0].on, FIXTURE_DATE);
        QCOMPARE(days[1].on, QDate(2026, 9, 17));
    }

    // Object ordering is a JSON serialisation detail; a UI must not depend on it.
    void daysComeBackInDateOrder()
    {
        const auto days = menus();
        for (qsizetype i = 1; i < days.size(); ++i)
            QVERIFY(days[i - 1].on < days[i].on);
    }

    void eachDayHasTheFullSetOfStations()
    {
        const auto days = menus();
        QCOMPARE(days[0].blocks.size(), 20);
        QVERIFY(days[0].venues().contains(HOME_COOKING));
    }

    // ---- Home Cooking — the thing the screen is actually for ----------------

    void homeCookingHasAllThreeMeals()
    {
        QCOMPARE(meals(menus()[0].forVenue(HOME_COOKING)), (QStringList{"Breakfast", "Lunch", "Dinner"}));
    }

    // Dinner listed before breakfast reads as a bug even when it's accurate.
    void mealsAreOrderedChronologicallyNotAsReceived()
    {
        const DayMenu scrambled{FIXTURE_DATE,
                                {block(HOME_COOKING, "Dinner", "dinner"),
                                 block(HOME_COOKING, "Breakfast", "breakfast"),
                                 block(HOME_COOKING, "Lunch", "lunch")}};
        QCOMPARE(meals(scrambled.forVenue()), (QStringList{"Breakfast", "Lunch", "Dinner"}));
    }

    // Regression, found against the real payload.
    //
    // `meal` is not a sitting. In the live data it is sometimes null (Grille,
    // Italian), sometimes the sitting ("Breakfast"), and sometimes a
    // sub-station name — "yogurt bar" under Breakfast All Day, "Deli" under
    // Allergen Aware. Sorting on it pushed "yogurt bar", a breakfast block,
    // past dinner.
    void orderingKeysOnSlotNotOnTheFreeTextMealLabel()
    {
        const MenuBlock yogurt = block("Breakfast All Day", "yogurt bar", "breakfast");
        const MenuBlock dinner = block(HOME_COOKING, "Dinner", "dinner");
        const MenuBlock allDay = block("Grille", "", "anytime");
        QVERIFY(yogurt.sortKey() < dinner.sortKey());
        QVERIFY(dinner.sortKey() < allDay.sortKey());
    }

    void headingFallsBackToTheSlotWhenMealIsMissing()
    {
        QCOMPARE(block("Grille", "", "anytime").heading(), QStringLiteral("All day"));
        QCOMPARE(block("X", "", "lunch").heading(), QStringLiteral("Lunch"));
        // …but a more specific upstream label wins.
        QCOMPARE(block("X", "Deli", "anytime").heading(), QStringLiteral("Deli"));
        // An unknown slot is title-cased rather than shown raw.
        QCOMPARE(block("X", "", "late night").heading(), QStringLiteral("Late Night"));
    }

    void realDishesComeThrough()
    {
        const MenuBlock breakfast = menus()[0].forVenue(HOME_COOKING)[0];
        QStringList names;
        for (const auto &i : breakfast.items)
            names.append(i.name);
        QVERIFY(names.contains("Chorizo Sausage Patties"));
        QVERIFY(names.contains("Mexican Potatoes"));
    }

    // The API sends {url, alt} icon objects; only the alt text is useful.
    void allergenIconsBecomePlainLabels()
    {
        const auto blocks = menus()[0].forVenue(HOME_COOKING);
        const auto cheese = findItem({blocks[0]}, "Shredded Cheese");
        QVERIFY(cheese);
        QCOMPARE(cheese->allergens, QStringList{"dairy"});
        QCOMPARE(cheese->allergenText(), QStringLiteral("dairy"));
    }

    void itemsWithNoAllergensAreEmpty()
    {
        const auto blocks = menus()[0].forVenue(HOME_COOKING);
        const auto plain = findItem({blocks[0]}, "Chorizo Sausage Patties");
        QVERIFY(plain);
        QVERIFY(plain->allergens.isEmpty());
        QCOMPARE(plain->allergenText(), QString());
    }

    void multipleAllergensAreAllKept()
    {
        const auto gravy = findItem(menus()[1].forVenue(HOME_COOKING), "Biscuits & Country Gravy");
        QVERIFY(gravy);
        QCOMPARE(QSet<QString>(gravy->allergens.begin(), gravy->allergens.end()),
                 (QSet<QString>{"gluten", "dairy", "egg", "soy"}));
    }

    // The real payload contains "Eggs with Peppers and  Onion" — two spaces.
    void doubledSpacesInUpstreamNamesAreNormalised()
    {
        const auto blocks = menus()[0].forVenue(HOME_COOKING);
        QVERIFY(findItem({blocks[0]}, "Eggs with Peppers and Onion"));
    }

    void homeCookingForPicksTheRightDay()
    {
        QCOMPARE(meals(homeCookingFor(menus(), FIXTURE_DATE)), (QStringList{"Breakfast", "Lunch", "Dinner"}));
    }

    // The API only serves forward from today, so asking about yesterday
    // legitimately returns nothing. That is not a failure.
    void homeCookingForAMissingDayIsEmptyNotAnError()
    {
        QVERIFY(homeCookingFor(menus(), QDate(2020, 1, 1)).isEmpty());
    }

    void venueLookupIsCaseInsensitive()
    {
        const DayMenu day = menus()[0];
        QCOMPARE(day.forVenue("home cooking"), day.forVenue(HOME_COOKING));
    }

    // ---- All-day stations ----------------------------------------------------

    // Grille, Italian, SubZone etc. carry slot: "anytime".
    void allDayStationsSortLast()
    {
        QList<MenuBlock> anytime;
        const QList<DayMenu> days = menus();
        for (const auto &b : days[0].blocks) {
            if (b.slot == "anytime")
                anytime.append(b);
        }
        QVERIFY2(!anytime.isEmpty(), "the capture should contain all-day stations");
        bool anyUnlabelled = false, anyLabelled = false;
        for (const auto &b : anytime) {
            QCOMPARE(b.sortKey(), int(SLOT_ORDER.size()));
            anyUnlabelled = anyUnlabelled || b.meal.isEmpty();
            anyLabelled = anyLabelled || !b.meal.isEmpty();
        }
        // Most, but NOT all, of them have a null meal — "Deli" under Allergen
        // Aware is an all-day block with a label. That is why `meal` cannot
        // drive sorting.
        QVERIFY(anyUnlabelled);
        QVERIFY(anyLabelled);
    }

    void everyBlockHasOneOfTheFourKnownSlots()
    {
        const QSet<QString> known{"breakfast", "lunch", "dinner", "anytime"};
        for (const auto &day : menus()) {
            for (const auto &b : day.blocks)
                QVERIFY2(known.contains(b.slot), qPrintable(b.slot));
        }
    }

    // ---- Robustness ----------------------------------------------------------

    // A menu missing one station is still a useful menu.
    void aMalformedBlockIsSkippedNotFatal()
    {
        const auto days = parseMenus(parse(R"({"2026-09-16": [
            "this is not a block",
            {"no_venue": true},
            {"venue": "Home Cooking", "meal": "Lunch", "slot": "lunch",
             "items": [{"name": "Soup"}, "not an item", {"no_name": 1}]}]})"));
        QCOMPARE(days.size(), 1);
        QCOMPARE(days[0].blocks.size(), 1);
        QCOMPARE(days[0].blocks[0].items.size(), 1);
        QCOMPARE(days[0].blocks[0].items[0].name, QStringLiteral("Soup"));
    }

    void anUnparseableDateKeyIsSkipped()
    {
        const auto days = parseMenus(parse(R"({"not-a-date": [{"venue": "Grille"}],
                                               "2026-09-16": [{"venue": "Home Cooking"}]})"));
        QCOMPARE(days.size(), 1);
        QCOMPARE(days[0].on, QDate(2026, 9, 16));
    }

    void aWhollyUnusablePayloadThrows()
    {
        QVERIFY_THROWS_EXCEPTION(ParseError, parseMenus(parse(R"({"not-a-date": []})")));
    }

    void aListPayloadThrowsRatherThanReturningNothing()
    {
        CV_VERIFY_THROWS_MATCHING(parseMenus(parse(R"([{"venue": "Home Cooking"}])")), ParseError, "date");
    }

    // The dining site's own menu.js still reads `tags`; the API sends
    // `allergens`. Accepting both costs one line and future-proofs a rename.
    void legacyTagsKeyStillWorks()
    {
        const auto days = parseMenus(parse(R"({"2026-09-16": [{"venue": "Home Cooking", "meal": "Lunch",
            "items": [{"name": "Pizza", "tags": ["gluten", "dairy"]}]}]})"));
        QCOMPARE(days[0].blocks[0].items[0].allergens, (QStringList{"gluten", "dairy"}));
    }

    void duplicateAllergensAreCollapsed()
    {
        const auto days = parseMenus(parse(R"({"2026-09-16": [{"venue": "X", "items": [{"name": "Y",
            "allergens": [{"alt": "dairy"}, {"alt": "dairy"}, {"alt": "soy"}]}]}]})"));
        QCOMPARE(days[0].blocks[0].items[0].allergens, (QStringList{"dairy", "soy"}));
    }

    // ---- The provider --------------------------------------------------------

    // This is what makes TransportRouter send it over plain HTTP.
    void providerTargetsTheDiningOriginNotSelfservice()
    {
        QVERIFY(DiningProvider(nullptr).path().startsWith(DINING_BASE));
        QCOMPARE(DINING_PATH, DINING_BASE + "/api/menus");
    }

    void providerRequestsTheDaysItWasAskedFor()
    {
        QVERIFY(DiningProvider(nullptr, 3).path().endsWith("?days=3"));
    }

    void providerAsksForAStartDateOnlyWhenGivenOne()
    {
        QVERIFY(!DiningProvider(nullptr, 7).path().contains("start"));
        QVERIFY(DiningProvider(nullptr, 7, QDate(2026, 9, 1)).path().endsWith("?days=7&start=2026-09-01"));
    }

    // The server's "No Venues Found" placeholder, verbatim from 2026-12-25.
    void aDayWithNothingPostedParsesToAnEmptyHomeCooking()
    {
        const auto days = parseMenus(parse(R"({"2026-12-25": [
            {"venue": "No Venues Found", "meal": null, "slot": "anytime", "items": []}]})"));
        QCOMPARE(days[0].on, QDate(2026, 12, 25));
        QVERIFY(days[0].forVenue(HOME_COOKING).isEmpty());
    }

    void daysIsClampedToSomethingSane()
    {
        QCOMPARE(DiningProvider(nullptr, 0).days(), 1);
        QCOMPARE(DiningProvider(nullptr, -5).days(), 1);
    }

    // `refresh=1` forces an upstream refetch from Pioneer College Caterers. A
    // personal app has no business making someone else's server work harder.
    void providerNeverSendsRefresh() { QVERIFY(!DiningProvider(nullptr, 7).path().contains("refresh")); }

    void providerEndToEndOverTheFixture()
    {
        const auto days = DiningProvider(std::make_shared<FixtureTransport>(testing::fixturesDir())).fetch();
        QCOMPARE(days.size(), 2);
        QVERIFY(!days[0].forVenue(HOME_COOKING)[0].items.isEmpty());
    }

    // ---- The next sitting ----------------------------------------------------
    //
    // What the summary screen puts on the front page. The menu feed carries no
    // serving times, so the hours are hardcoded — which is exactly why they
    // need tests: a rule nobody can look up is a rule that drifts. The
    // fixture's days are a Wednesday and a Thursday, so these are weekday
    // hours.

    void beforeHalfNineTheNextMealIsBreakfast()
    {
        const auto found = nextMealBlock(menus(), at(FIXTURE_DATE, 7, 30));
        QVERIFY(found);
        QCOMPARE(found->first, FIXTURE_DATE);
        QCOMPARE(found->second.slot, QStringLiteral("breakfast"));
    }

    void betweenBreakfastAndLunchItIsAlreadyLunch()
    {
        QCOMPARE(nextSlot(menus(), at(FIXTURE_DATE, 10, 0)), QStringLiteral("lunch"));
    }

    void afterLunchClosesItIsDinner()
    {
        QCOMPARE(nextSlot(menus(), at(FIXTURE_DATE, 14, 29)), QStringLiteral("lunch"));
        QCOMPARE(nextSlot(menus(), at(FIXTURE_DATE, 14, 31)), QStringLiteral("dinner"));
    }

    // The alternative is showing a menu for a meal that is already over.
    void afterDinnerItRollsOverToTomorrow()
    {
        QCOMPARE(nextSlot(menus(), at(FIXTURE_DATE, 19, 29)), QStringLiteral("dinner"));
        const auto found = nextMealBlock(menus(), at(FIXTURE_DATE, 19, 31));
        QCOMPARE(found->first, QDate(2026, 9, 17));
        QCOMPARE(found->second.slot, QStringLiteral("breakfast"));
    }

    // Breakfast ends at 9:30 on a weekday; by then it is lunch that is next.
    void theCardMovesOnAsSoonAsBreakfastCloses()
    {
        QCOMPARE(nextSlot(menus(), at(FIXTURE_DATE, 9, 29)), QStringLiteral("breakfast"));
        QCOMPARE(nextSlot(menus(), at(FIXTURE_DATE, 9, 30)), QStringLiteral("lunch"));
        QCOMPARE(nextSlot(menus(), at(FIXTURE_DATE, 9, 31)), QStringLiteral("lunch"));
    }

    void saturdayRunsOnSaturdayHours()
    {
        const QDate saturday(2026, 9, 19);
        const QList<DayMenu> days{fullDay(saturday)};
        QCOMPARE(nextSlot(days, at(saturday, 8, 59)), QStringLiteral("breakfast"));
        QCOMPARE(nextSlot(days, at(saturday, 9, 1)), QStringLiteral("lunch"));
        QCOMPARE(nextSlot(days, at(saturday, 13, 1)), QStringLiteral("dinner"));
    }

    void sundayRunsOnSundayHours()
    {
        const QDate sunday(2026, 9, 20);
        const QList<DayMenu> days{fullDay(sunday)};
        QCOMPARE(nextSlot(days, at(sunday, 9, 1)), QStringLiteral("lunch"));
        QCOMPARE(nextSlot(days, at(sunday, 13, 59)), QStringLiteral("lunch"));
        QCOMPARE(nextSlot(days, at(sunday, 14, 1)), QStringLiteral("dinner"));
    }

    void servingHoursFollowTheDayOfTheWeek()
    {
        QCOMPARE(servingHours(QDate(2026, 9, 16), "breakfast"), ServingHours(QTime(7, 0), QTime(9, 30)));
        QCOMPARE(servingHours(QDate(2026, 9, 18), "Lunch"), ServingHours(QTime(10, 30), QTime(14, 30)));
        QCOMPARE(servingHours(QDate(2026, 9, 19), "lunch"), ServingHours(QTime(11, 0), QTime(13, 0)));
        QCOMPARE(servingHours(QDate(2026, 9, 19), "dinner"), ServingHours(QTime(16, 30), QTime(18, 30)));
        QCOMPARE(servingHours(QDate(2026, 9, 20), "lunch"), ServingHours(QTime(11, 30), QTime(14, 0)));
        QCOMPARE(servingHours(QDate(2026, 9, 20), "dinner"), ServingHours(QTime(17, 0), QTime(19, 30)));
    }

    void allDayStationsHaveNoServingHours() { QVERIFY(!servingHours(QDate(2026, 9, 16), "anytime")); }

    void hoursReadLikeThePostedSign()
    {
        QCOMPARE(formatHours(QTime(10, 30), QTime(14, 30)), QStringLiteral("10:30am–2:30pm"));
        QCOMPARE(formatHours(QTime(7, 0), QTime(9, 30)), QStringLiteral("7am–9:30am"));
        QCOMPARE(formatHours(QTime(11, 0), QTime(13, 0)), QStringLiteral("11am–1pm"));
    }

    // The API serves forward from today, so this happens with a stale cache.
    void nothingLeftInThePayloadIsNotAnError()
    {
        QVERIFY(!nextMealBlock(menus(), at(QDate(2026, 9, 20), 7, 0)));
    }

    // "Breakfast All Day" is always on; it is not a sitting you can be before.
    void allDayStationsAreNeverUpNext()
    {
        const DayMenu day{QDate(2026, 9, 16),
                          {block(HOME_COOKING, "yogurt bar", "anytime", {"Granola"}),
                           block(HOME_COOKING, "Lunch", "lunch", {"Pork Loin"})}};
        QCOMPARE(nextSlot({day}, at(QDate(2026, 9, 16), 7, 0)), QStringLiteral("lunch"));
    }

    // A heading with no dishes under it reads as a bug, not as a menu.
    void anEmptySittingIsSkippedForTheNextRealOne()
    {
        const DayMenu day{QDate(2026, 9, 16),
                          {block(HOME_COOKING, "Breakfast", "breakfast"),
                           block(HOME_COOKING, "Lunch", "lunch", {"Pork Loin"})}};
        QCOMPARE(nextSlot({day}, at(QDate(2026, 9, 16), 7, 0)), QStringLiteral("lunch"));
    }

    void onlyTheAskedForStationIsConsidered()
    {
        QCOMPARE(nextMealBlock(menus(), at(FIXTURE_DATE, 7, 30))->second.venue, HOME_COOKING);
    }
};

CEDARVIEW_TEST_MAIN(TestDiningProvider, QCoreApplication)
#include "tst_dining_provider.moc"
