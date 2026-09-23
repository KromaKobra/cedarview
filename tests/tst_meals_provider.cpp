// Meal plan, against a REAL captured page and endpoint.
//
// Fixtures, both from a signed-in capture on 2026-09-22 (`scripts/discover meals`):
//
// * `cedarinfo_meals.html` — the Vue page, trimmed. It carries no figures,
//   only `data-target-id` (scrubbed to 0000000).
// * `cedarinfo_meals_getbalancejson.json` — what
//   `/CedarInfo/Meals/GetBalanceJson` returned. Balances and plan name are
//   verbatim, because they are the thing being parsed; the transaction rows are
//   replaced with made-up ones of the same shape.

#include "testsupport.h"

#include "core/htmlattrs.h"
#include "core/providers/meals.h"

using namespace mycu;

namespace {

QJsonObject balance()
{
    return testing::fixtureJson("cedarinfo_meals_getbalancejson.json").toObject();
}

MealPlan plan()
{
    return parseBalance(testing::toJson(balance()));
}

QJsonObject tender(const QString &name, const QString &kind, const QJsonValue &amount, bool currency)
{
    return QJsonObject{{"Name", name}, {"Type", kind}, {"Amount", amount}, {"IsCurrency", currency}};
}

QString withBalances(QJsonObject base, const QJsonArray &tenders, const QJsonObject &fields = {})
{
    base.insert("Balances", tenders);
    for (auto it = fields.begin(); it != fields.end(); ++it)
        base.insert(it.key(), it.value());
    return testing::toJson(base);
}

QJsonObject row(const QString &date, const QString &activity = "Board meal",
                const QString &period = "Dinner", const QJsonValue &amount = QJsonValue::Null,
                bool deposit = false)
{
    return QJsonObject{{"Date", date}, {"Activity", activity}, {"MealPeriod", period},
                       {"Amount", amount}, {"IsDeposit", deposit}};
}

// Serves the fixtures and remembers what was asked for, in order.
class RecordingTransport : public Transport
{
public:
    explicit RecordingTransport(QString loginOn = QString()) : loginOn(std::move(loginOn)) {}

    Response get(const QString &path) override
    {
        paths.append(path);
        if (!loginOn.isEmpty() && path.contains(loginOn))
            return Response{200, "https://login.microsoftonline.com/x/saml2", "<html>SAMLRequest</html>", {}};
        return inner.get(path);
    }

    FixtureTransport inner{testing::fixturesDir()};
    QString loginOn;
    QStringList paths;
};

} // namespace

class TestMealsProvider : public QObject
{
    Q_OBJECT

private slots:
    // ---- The figures ---------------------------------------------------------

    void mealsRemaining() { QCOMPARE(plan().mealsRemaining, 16); }

    // The new page's "Flex Dollars" is the old "Meal Plan Dining Dollars".
    //
    // Same account, five days apart: $112.08 before, $102.34 after two flex
    // purchases of $3.74 and $6.00. It expires at term end, so it is
    // diningDollars (Temporary Flex), not the purchased kind.
    void flexDollarsAreThePlansExpiringBalance() { QCOMPARE(plan().diningDollars, 102.34); }

    // The endpoint lists no voluntary tender; "$0.00" would be a guess.
    void anAbsentVoluntaryBalanceIsNotReportedRatherThanZero()
    {
        QVERIFY(!plan().flexDollars);
        QCOMPARE(MealPlan::money(plan().flexDollars), QString());
    }

    // "Meal Exchange" also has an Amount of 16 in the capture; it must not be
    // what fills either figure, whatever the order.
    void mealExchangesAreNotMistakenForMealsOrMoney() { QVERIFY(plan().diningDollars != 16.0); }

    void aVoluntaryBalanceIsRecognisedByName()
    {
        const MealPlan p = parseBalance(withBalances(balance(), {
            tender("Voluntary Flex Dollars", "DEBIT", 25, true),
            tender("Flex Dollars", "DEBIT", 80.5, true),
            tender("Board Meals", "MEAL", 3, false),
        }));
        QCOMPARE(p.flexDollars, 25.0);
        QCOMPARE(p.diningDollars, 80.5);
        QCOMPARE(p.mealsRemaining, 3);
    }

    void tendersAreMatchedByTypeNotPosition()
    {
        QJsonArray reversed;
        for (const QJsonValue &t : balance().value("Balances").toArray())
            reversed.prepend(t);
        const MealPlan p = parseBalance(withBalances(balance(), reversed));
        QCOMPARE(p.mealsRemaining, 16);
        QCOMPARE(p.diningDollars, 102.34);
    }

    void aZeroBalanceIsZeroNotMissing()
    {
        const MealPlan p = parseBalance(withBalances(balance(), {tender("Flex Dollars", "DEBIT", 0, true)}));
        QVERIFY(p.diningDollars.has_value());
        QCOMPARE(*p.diningDollars, 0.0);
    }

    void aNullAmountIsNotReported()
    {
        const MealPlan p = parseBalance(withBalances(balance(), {
            tender("Flex Dollars", "DEBIT", QJsonValue::Null, true),
            tender("Board Meals", "MEAL", 4, false),
        }));
        QVERIFY(!p.diningDollars);
        QCOMPARE(p.mealsRemaining, 4);
    }

    // ---- Plan name and cycle -------------------------------------------------
    //
    // Read off PlanName rather than assumed. Weekly plans and per-term block
    // plans both exist, and telling a block-plan holder their meals reset on
    // Sunday would be a wrong statement about their own account.

    void theRealPlanIsWeekly()
    {
        const MealPlan p = plan();
        QCOMPARE(p.planName, QStringLiteral("21 Meals"));
        QCOMPARE(p.period, QStringLiteral("week"));
        QCOMPARE(p.periodText(), QStringLiteral("this week"));
        QCOMPARE(p.planDescription(), QStringLiteral("21 Meals per week"));
    }

    void aBlockPlanIsPerTerm()
    {
        const MealPlan p = parseBalance(withBalances(balance(), {tender("Board Meals", "MEAL", 90, false)},
                                                     {{"PlanName", "Block 120"}}));
        QCOMPARE(p.period, QStringLiteral("term"));
        QCOMPARE(p.planDescription(), QStringLiteral("Block 120"));
    }

    void anUnrecognisedPlanNameSaysNothingAboutTheCycle()
    {
        const MealPlan p = parseBalance(withBalances(balance(), {tender("Board Meals", "MEAL", 7, false)},
                                                     {{"PlanName", "Commuter Special"}}));
        QCOMPARE(p.period, QString());
        QCOMPARE(p.periodText(), QString());
        QCOMPARE(p.planDescription(), QStringLiteral("Commuter Special"));
        QCOMPARE(p.mealsRemaining, 7); // still parsed; only the cycle is unknown
    }

    void noPlanNameFallsBackToTheCycleOrNothing()
    {
        MealPlan weekly;
        weekly.period = "week";
        QCOMPARE(weekly.planDescription(), QStringLiteral("Weekly meal plan"));
        MealPlan term;
        term.period = "term";
        QCOMPARE(term.planDescription(), QStringLiteral("Semester meal plan"));
        QCOMPARE(MealPlan().planDescription(), QString());
    }

    // ---- Answers that are not balances ---------------------------------------

    void noPlanOnFileIsAnEmptyPlanNotAnError()
    {
        QVERIFY(!parseBalance(withBalances(balance(), {}, {{"Found", false}})).hasAny());
    }

    void noIdCardIsAnEmptyPlanNotAnError()
    {
        QVERIFY(!parseBalance(R"({"Status": "no_card", "Message": "No card", "Found": false})").hasAny());
    }

    void aServerErrorIsReportedWithItsMessage()
    {
        CV_VERIFY_THROWS_MATCHING(parseBalance(R"({"Status": "error", "Message": "database unavailable"})"),
                                  ParseError, "database unavailable");
    }

    void anUnknownStatusIsNamed()
    {
        CV_VERIFY_THROWS_MATCHING(parseBalance(R"({"Status": "weird"})"), ParseError, "'weird'");
    }

    void aNonAnswerThrows_data()
    {
        QTest::addColumn<QString>("body");
        QTest::newRow("empty") << "";
        QTest::newRow("blank") << "   ";
        QTest::newRow("html") << "<html>login</html>";
        QTest::newRow("list") << "[]";
    }
    void aNonAnswerThrows()
    {
        QFETCH(QString, body);
        QVERIFY_THROWS_EXCEPTION(ParseError, parseBalance(body));
    }

    void aReshapedResponseThrowsRatherThanShowingBlanks()
    {
        QJsonObject reshaped = balance();
        reshaped.insert("Tenders", reshaped.take("Balances"));
        CV_VERIFY_THROWS_MATCHING(parseBalance(testing::toJson(reshaped)), ParseError, "Balances");
    }

    void unrecognisedTendersThrowAndNameThemselves()
    {
        CV_VERIFY_THROWS_MATCHING(parseBalance(withBalances(balance(), {tender("Swipes", "SWIPE", 4, false)})),
                                  ParseError, "'Swipes'");
    }

    // ---- The page: only who to look up ---------------------------------------

    void theTargetIsReadOffTheRealPage()
    {
        QCOMPARE(parseTarget(testing::fixture("cedarinfo_meals.html")), (MealsTarget{"0000000", ""}));
    }

    void aPageWithoutATargetThrows()
    {
        CV_VERIFY_THROWS_MATCHING(
            parseTarget("<html><body><p>You have <strong>19</strong> meals.</p></body></html>"),
            ParseError, "data-target-id");
    }

    void anEmptyPageThrows() { QVERIFY_THROWS_EXCEPTION(ParseError, parseTarget("  \n")); }

    // The attribute finder knows exactly as much HTML as the page needs.
    void theTargetIsFoundWhereverTheMarkupPutsIt()
    {
        QCOMPARE(parseTarget("<div DATA-TARGET-ID='42' data-target-card=\"7\">"), (MealsTarget{"42", "7"}));
        QCOMPARE(parseTarget("<cu-container data-target-id=42 id=app>"), (MealsTarget{"42", ""}));
        QCOMPARE(parseTarget("<x data-target-id=\" 42 \" data-target-card>"), (MealsTarget{"42", ""}));
        QCOMPARE(parseTarget("<x data-target-id=\"a&amp;b\">"), (MealsTarget{"a&b", ""}));
    }

    // A tag inside a script string or a comment is text, not markup.
    void scriptsAndCommentsAreNotMarkup()
    {
        const QString page = QStringLiteral(
            "<!-- <div data-target-id=\"comment\"> -->"
            "<script>var t = '<div data-target-id=\"script\">';</script>"
            "<style>.x{content:'<div data-target-id=\"style\">'}</style>"
            "<main><cu-container data-target-id=\"real\"></cu-container></main>");
        QCOMPARE(parseTarget(page).personId, QStringLiteral("real"));
    }

    void theBalanceUrlSendsOnlyWhatThePageSends()
    {
        // The recorded request was ?id=… alone: the empty card is left off.
        QCOMPARE(balancePath({"0000000", ""}), BALANCE_PATH + "?id=0000000");
        QCOMPARE(balancePath({"", "12345"}), BALANCE_PATH + "?card=12345");
        QCOMPARE(balancePath({}), BALANCE_PATH);
        QCOMPARE(balancePath({"a b", "1&2"}), BALANCE_PATH + "?id=a+b&card=1%262");
    }

    // ---- The provider --------------------------------------------------------

    // Not Transact: same origin and same sign-in as chapel.
    void providerPathIsOnSelfservice()
    {
        QCOMPARE(MealsProvider::path, MEALS_PATH);
        QCOMPARE(MEALS_PATH, QStringLiteral("/Cedarinfo/Meals"));
        QVERIFY(BALANCE_PATH.startsWith("/CedarInfo/Meals/"));
    }

    void providerEndToEndOverTheFixtures()
    {
        const MealPlan p = MealsProvider(std::make_shared<FixtureTransport>(testing::fixturesDir())).fetch();
        QCOMPARE(p.mealsRemaining, 16);
        QCOMPARE(p.diningDollars, 102.34);
        QVERIFY(!p.flexDollars);
        QCOMPARE(p.planDescription(), QStringLiteral("21 Meals per week"));
    }

    void providerAsksForThePageThenTheIdItNames()
    {
        auto transport = std::make_shared<RecordingTransport>();
        MealsProvider(transport).fetch();
        QCOMPARE(transport->paths, (QStringList{MEALS_PATH, BALANCE_PATH + "?id=0000000"}));
    }

    void anExpiredSessionOnEitherRequestIsReportedAsOne_data()
    {
        QTest::addColumn<QString>("expiresOn");
        QTest::newRow("page") << MEALS_PATH;
        QTest::newRow("balance") << BALANCE_PATH;
    }
    void anExpiredSessionOnEitherRequestIsReportedAsOne()
    {
        QFETCH(QString, expiresOn);
        QVERIFY_THROWS_EXCEPTION(SessionExpired,
                                 MealsProvider(std::make_shared<RecordingTransport>(expiresOn)).fetch());
    }

    // ---- Recent activity -----------------------------------------------------

    void everyTransactionIsReadNewestFirst()
    {
        const MealPlan p = plan();
        QCOMPARE(p.transactions.size(), balance().value("RecentTransactions").toArray().size());
        for (qsizetype i = 1; i < p.transactions.size(); ++i)
            QVERIFY(p.transactions[i - 1].at >= p.transactions[i].at);
        // The fixture keeps one out-of-order pair, as delivered; it must come
        // out sorted.
        QCOMPARE(p.transactions[3].activity, QStringLiteral("Flex purchase"));
        QCOMPARE(p.transactions[4].activity, QStringLiteral("Meal exchange"));
    }

    void aSwipeMovesNoMoney()
    {
        const MealTransaction swipe = plan().transactions[0];
        QCOMPARE(swipe, (MealTransaction{QDateTime(QDate(2026, 1, 2), QTime(19, 5)), "Board meal", "Dinner",
                                         std::nullopt, false}));
        QVERIFY(!swipe.amount);
        QVERIFY(!swipe.isFlex());
        QCOMPARE(swipe.amountText(), QString());
    }

    void aFlexPurchaseIsFlexAndReadsAsSpent()
    {
        for (const auto &t : plan().transactions) {
            if (t.activity == "Flex purchase") {
                QVERIFY(t.isFlex());
                QCOMPARE(t.amount, 3.74);
                QCOMPARE(t.amountText(), QStringLiteral("−$3.74"));
                return;
            }
        }
        QFAIL("no flex purchase in the fixture");
    }

    void aDepositIsFlexAndReadsAsAdded()
    {
        QJsonObject b = balance();
        b.insert("RecentTransactions", QJsonArray{row("2026-01-03T09:00:00", "Deposit", "", 20, true)});
        const MealPlan p = parseBalance(testing::toJson(b));
        QCOMPARE(p.transactions.size(), 1);
        QVERIFY(p.transactions[0].isFlex());
        QVERIFY(p.transactions[0].isDeposit);
        QCOMPARE(p.transactions[0].amountText(), QStringLiteral("+$20.00"));
    }

    void missingActivityIsNoActivityNotAnError()
    {
        QJsonObject b = balance();
        b.remove("RecentTransactions");
        const MealPlan p = parseBalance(testing::toJson(b));
        QVERIFY(p.transactions.isEmpty());
        QCOMPARE(p.mealsRemaining, 16);
    }

    void malformedRowsAreDroppedOrUndatedNotFatal()
    {
        QJsonObject b = balance();
        b.insert("RecentTransactions", QJsonArray{
            row("not a date"),
            "not a row",
            row("2026-01-02T12:00:00", "Flex purchase", "Dinner", true),
            row("2026-01-03T12:00:00"),
        });
        const MealPlan p = parseBalance(testing::toJson(b));
        QCOMPARE(p.transactions.size(), 3);
        QCOMPARE(p.transactions[0].at, QDateTime(QDate(2026, 1, 3), QTime(12, 0)));
        QCOMPARE(p.transactions[1].at, QDateTime(QDate(2026, 1, 2), QTime(12, 0)));
        QVERIFY(!p.transactions[2].at.isValid());
        // A boolean is not an amount; the name still marks it as flex.
        QVERIFY(!p.transactions[1].amount);
        QVERIFY(p.transactions[1].isFlex());
    }

    // ---- Formatting ----------------------------------------------------------

    void moneyFormatting()
    {
        QCOMPARE(MealPlan::money(112.08), QStringLiteral("$112.08"));
        QCOMPARE(MealPlan::money(0.0), QStringLiteral("$0.00"));
        QCOMPARE(MealPlan::money(1234.5), QStringLiteral("$1,234.50"));
        QCOMPARE(MealPlan::money(1234567.891), QStringLiteral("$1,234,567.89"));
        QCOMPARE(MealPlan::money(999.999), QStringLiteral("$1,000.00"));
    }

    // "$0.00" for an unknown balance would be a confident lie.
    void moneyForAMissingValueIsBlankNotZero() { QCOMPARE(MealPlan::money(std::nullopt), QString()); }

    void hasAny()
    {
        QVERIFY(!MealPlan().hasAny());
        MealPlan zeroMeals;
        zeroMeals.mealsRemaining = 0;
        QVERIFY(zeroMeals.hasAny());
        MealPlan zeroFlex;
        zeroFlex.flexDollars = 0.0;
        QVERIFY(zeroFlex.hasAny());
    }
};

CEDARVIEW_TEST_MAIN(TestMealsProvider, QCoreApplication)
#include "tst_meals_provider.moc"
