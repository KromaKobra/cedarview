// Chapel skips, against REAL captured responses.
//
// Fixtures are verbatim captures from a signed-in session on 2026-09-17, with
// the student ID scrubbed to 1234567 and the name to "Sample Student".
// Structure, counts and vocabulary are untouched — those are the thing being
// tested.

#include "testsupport.h"

#include "core/providers/chapel.h"

#include <functional>

using namespace mycu;

namespace {

QJsonValue summaryJson()
{
    return testing::fixtureJson("cedarinfo_chapelskip_getstudentsummaryjson.json");
}

QJsonValue ledgerJson()
{
    return testing::fixtureJson("cedarinfo_chapelskip_getstudentledgerjson.json");
}

ChapelSummary summary()
{
    return buildSummary(summaryJson(), ledgerJson(), QJsonArray());
}

QJsonValue parse(const char *text)
{
    const QJsonDocument doc = QJsonDocument::fromJson(text);
    return doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
}

// Serves the fixtures, remembers what was asked for, and can be told to fail.
class Recording : public Transport
{
public:
    std::function<Response(const QString &)> handler;
    QStringList seen;

    Response get(const QString &path) override
    {
        seen.append(path);
        if (handler)
            return handler(path);
        return inner.get(path);
    }

    FixtureTransport inner{testing::fixturesDir()};
};

const ChapelLedgerEntry *find(const QList<ChapelLedgerEntry> &entries,
                              std::function<bool(const ChapelLedgerEntry &)> pred)
{
    for (const ChapelLedgerEntry &e : entries) {
        if (pred(e))
            return &e;
    }
    return nullptr;
}

} // namespace

class TestChapelProvider : public QObject
{
    Q_OBJECT

private slots:
    // ---- The numbers ---------------------------------------------------------

    void reportedFiguresAreUsedVerbatim()
    {
        const ChapelSummary s = summary();
        QCOMPARE(s.used, 2);
        QCOMPARE(s.total, 18);
        QCOMPARE(s.remaining, 16);
    }

    // The single most important behaviour in this provider.
    //
    // The ledger's counts sum to 1 (+1, -1, +1). The server reports
    // SkipsUsed: 2. Both are right on the server's terms — the -1 manual
    // adjustment is *also* expressed as the +1 "Manual Arrangement" allowance
    // line. Recomputing from the ledger shows a different, wrong number.
    void theLedgerMustNotBeUsedToComputeSkipsUsed()
    {
        int sum = 0;
        for (const QJsonValue &e : ledgerJson().toArray())
            sum += e.toObject().value("Count").toInt();
        QCOMPARE(sum, 1);
        QVERIFY2(summary().used == 2, "used must come from SkipsUsed, not from the ledger");
    }

    void theAllowanceBreakdownSumsToTheTotal()
    {
        const ChapelSummary s = summary();
        QCOMPARE(s.allowance.size(), 2);
        QCOMPARE(s.allowance[0].reason, QStringLiteral("Skips Allowed"));
        QCOMPARE(s.allowance[1].reason, QStringLiteral("Manual Arrangement"));
        QCOMPARE(s.allowance[0].count, 17);
        QCOMPARE(s.allowance[1].count, 1);
        QCOMPARE(s.allowance[0].count + s.allowance[1].count, *s.total);
    }

    void allowanceDescriptionsSurvive()
    {
        QCOMPARE(summary().allowance[0].description, QStringLiteral("Base semester allowance"));
    }

    // ---- Identity and term ---------------------------------------------------

    void termAndName()
    {
        const ChapelSummary s = summary();
        QCOMPARE(s.term, QStringLiteral("2026FA"));
        QCOMPARE(s.termName, QStringLiteral("Fall Semester 2026"));
        QCOMPARE(s.studentName, QStringLiteral("Sample Student"));
    }

    void labelPrefersTheReadableTerm()
    {
        QCOMPARE(summary().label(), QStringLiteral("Fall Semester 2026"));
        ChapelSummary codeOnly;
        codeOnly.term = "2026FA";
        QCOMPARE(codeOnly.label(), QStringLiteral("2026FA"));
    }

    void requirementFlags()
    {
        const ChapelSummary s = summary();
        QCOMPARE(s.isRequiredToAttend, true);
        QCOMPARE(s.isInGoodStanding, true);
        QCOMPARE(s.status, QStringLiteral("good"));
        QVERIFY(s.requirementReasons.contains("Undergraduate Student"));
        QCOMPARE(s.requirementReasons.size(), 3);
    }

    // ---- The ledger ----------------------------------------------------------

    void everyLedgerEntryIsParsed() { QCOMPARE(summary().entries.size(), 3); }

    void entriesAreNewestFirst()
    {
        const auto entries = summary().entries;
        for (qsizetype i = 1; i < entries.size(); ++i)
            QVERIFY(entries[i - 1].createdAt >= entries[i].createdAt);
    }

    void bothEntryTypesAppear()
    {
        QSet<QString> types;
        for (const auto &e : summary().entries)
            types.insert(e.entryType);
        QCOMPARE(types, (QSet<QString>{"Chapel Skip", "Manual Adjustment"}));
    }

    void aManualAdjustmentCanBeNegative()
    {
        const auto entries = summary().entries;
        const auto *adjustment = find(entries, [](auto &e) { return e.entryType == "Manual Adjustment"; });
        QVERIFY(adjustment);
        QCOMPARE(adjustment->count, -1);
        QCOMPARE(adjustment->isSkip(), false);
        QCOMPARE(adjustment->canRemove, true);
    }

    // ChapelDate is genuinely null for adjustments — they aren't tied to a chapel.
    void anAdjustmentHasNoChapelDate()
    {
        const auto entries = summary().entries;
        const auto *adjustment = find(entries, [](auto &e) { return e.entryType == "Manual Adjustment"; });
        QVERIFY(!adjustment->on.isValid());
        QCOMPARE(adjustment->reason, QStringLiteral("Had ID replaced"));
    }

    void anUndatedEntryStillHasSomethingTrueToShow()
    {
        const auto entries = summary().entries;
        const auto *adjustment = find(entries, [](auto &e) { return !e.on.isValid(); });
        QCOMPARE(adjustment->when(), QStringLiteral("Had ID replaced"));
    }

    void aSkipShowsItsDate()
    {
        const auto entries = summary().entries;
        const auto *skip = find(entries, [](auto &e) { return e.entryType == "Chapel Skip"; });
        QCOMPARE(skip->count, 1);
        QCOMPARE(skip->isSkip(), true);
        QVERIFY(skip->on.isValid());
        QVERIFY(skip->when().contains(QString::number(skip->on.date().year())));
    }

    void aSkipReadsLikeADate()
    {
        const auto entries = summary().entries;
        const auto *skip = find(entries, [](auto &e) { return e.on == QDateTime(QDate(2026, 8, 20), QTime(10, 0)); });
        QVERIFY(skip);
        QCOMPARE(skip->when(), QStringLiteral("Thu Aug 20, 2026"));
    }

    void chapelDatesParse()
    {
        QList<QDateTime> dates;
        for (const auto &e : summary().entries) {
            if (e.on.isValid())
                dates.append(e.on);
        }
        std::sort(dates.begin(), dates.end());
        QCOMPARE(dates.first(), QDateTime(QDate(2026, 8, 17), QTime(10, 0)));
        QCOMPARE(dates.last(), QDateTime(QDate(2026, 8, 20), QTime(10, 0)));
    }

    void createdAtKeepsItsMilliseconds()
    {
        const auto entries = summary().entries;
        QCOMPARE(entries.first().createdAt, QDateTime(QDate(2026, 8, 20), QTime(11, 11, 29, 623)));
    }

    void noFinesIsAnEmptyListNotAnError() { QVERIFY(summary().fines.isEmpty()); }

    // ---- The student-ID bootstrap -------------------------------------------

    void studentIdIsReadFromTheDashboard()
    {
        QCOMPARE(extractStudentId(testing::chapelHtml()), QStringLiteral("1234567"));
    }

    void studentIdSurvivesReasonableReformatting_data()
    {
        QTest::addColumn<QString>("snippet");
        QTest::newRow("const") << "const studentId = '1234567'";
        QTest::newRow("double quotes") << "const studentId=\"1234567\"";
        QTest::newRow("spacing") << "let studentId   =    '1234567' ;";
        QTest::newRow("var") << "var studentId='1234567'";
    }
    void studentIdSurvivesReasonableReformatting()
    {
        QFETCH(QString, snippet);
        QCOMPARE(extractStudentId("<script>" + snippet + "</script>"), QStringLiteral("1234567"));
    }

    void aMissingBootstrapSaysWhatToDo()
    {
        CV_VERIFY_THROWS_MATCHING(extractStudentId("<html><body>nothing here</body></html>"),
                                  ParseError, "discover chapel");
    }

    // ---- Robustness ----------------------------------------------------------

    void aNonObjectSummaryThrows()
    {
        QVERIFY_THROWS_EXCEPTION(ParseError, buildSummary(parse("[1, 2, 3]"), QJsonArray()));
    }

    void aNonListLedgerThrows()
    {
        CV_VERIFY_THROWS_MATCHING(buildSummary(parse(R"({"SkipsUsed": 0})"), parse(R"({"not": "a list"})")),
                                  ParseError, "list");
    }

    void anEmptyLedgerIsFine()
    {
        const ChapelSummary s = buildSummary(parse(R"({"SkipsUsed": 0, "SkipsTotal": 18, "SkipsRemaining": 18})"),
                                             QJsonArray());
        QVERIFY(s.entries.isEmpty());
        QCOMPARE(s.remaining, 18);
    }

    void aMalformedLedgerEntryIsSkipped()
    {
        const ChapelSummary s = buildSummary(QJsonObject(),
                                             parse(R"(["nonsense", {"Count": 1, "EntryType": "Chapel Skip"}])"));
        QCOMPARE(s.entries.size(), 1);
    }

    void anUnparseableTimestampCostsOnlyThatField()
    {
        const ChapelSummary s = buildSummary(
            QJsonObject(), parse(R"([{"Count": 1, "ChapelDate": "whenever", "CreatedReason": "x"}])"));
        QVERIFY(!s.entries[0].on.isValid());
        QCOMPARE(s.entries[0].reason, QStringLiteral("x"));
    }

    // Zero would render as "0 of 0 skips" — a confident lie.
    void missingFiguresStayUnreportedRatherThanBecomingZero()
    {
        const ChapelSummary s = buildSummary(QJsonObject(), QJsonArray());
        QVERIFY(!s.used);
        QVERIFY(!s.total);
        QVERIFY(!s.remaining);
    }

    // A bool is not a count, and a numeric string still is one.
    void countsAreReadDefensively()
    {
        const ChapelSummary s = buildSummary(
            parse(R"({"SkipsUsed": true, "SkipsTotal": "18 skips", "SkipsRemaining": 16.9})"), QJsonArray());
        QVERIFY(!s.used);
        QCOMPARE(s.total, 18);
        QCOMPARE(s.remaining, 16);
    }

    // ---- The provider, end to end -------------------------------------------

    void providerFetchesAllThreeEndpoints()
    {
        const ChapelSummary s =
            ChapelProvider(std::make_shared<FixtureTransport>(testing::fixturesDir())).fetch();
        QCOMPARE(s.used, 2);
        QCOMPARE(s.total, 18);
        QCOMPARE(s.entries.size(), 3);
    }

    // Four requests: the page for the ID, then summary, ledger and fines.
    void providerRequestsTheDashboardThenTheJson()
    {
        auto recording = std::make_shared<Recording>();
        ChapelProvider(recording).fetch();

        QCOMPARE(recording->seen[0], CHAPEL_PATH);
        QVERIFY(recording->seen[1].contains(SUMMARY_PATH));
        QVERIFY(recording->seen[1].contains("studentId=1234567"));
        QVERIFY(recording->seen[2].contains(LEDGER_PATH));
        QCOMPARE(recording->seen.size(), 4);
    }

    void theStudentIdIsFetchedOnceAndReused()
    {
        auto recording = std::make_shared<Recording>();
        ChapelProvider provider(recording);
        provider.fetch();
        provider.fetch();
        QVERIFY2(recording->seen.count(CHAPEL_PATH) == 1, "the dashboard should not be re-fetched");
    }

    // The count is the point of the screen; fines are a nice-to-have.
    void finesFailingDoesNotCostYouTheSkipCount()
    {
        auto broken = std::make_shared<Recording>();
        broken->handler = [&](const QString &path) -> Response {
            if (path.contains("Fines"))
                throw std::runtime_error("fines endpoint is having a day");
            return broken->inner.get(path);
        };
        const ChapelSummary s = ChapelProvider(broken).fetch();
        QCOMPARE(s.used, 2);
        QVERIFY(s.fines.isEmpty());
    }

    // A sign-in page must never reach a parser.
    void aLoginPageThrowsSessionExpiredInsteadOfParseError()
    {
        auto expired = std::make_shared<Recording>();
        expired->handler = [](const QString &) {
            return Response{200, "https://login.microsoftonline.com/81c32413-.../saml2?SAMLRequest=x",
                            testing::loginHtml(), {}};
        };
        QVERIFY_THROWS_EXCEPTION(SessionExpired, ChapelProvider(expired).fetch());
    }

    void theCanonicalPathIsUnchanged()
    {
        QCOMPARE(ChapelProvider::path, CHAPEL_PATH);
        QCOMPARE(CHAPEL_PATH, QStringLiteral("/cedarinfo/chapelskip"));
        QCOMPARE(BASE_URL, QStringLiteral("https://selfservice.cedarville.edu"));
    }

    void aSummaryOnItsOwnParses()
    {
        const ChapelSummary s =
            parseBody(testing::fixture("cedarinfo_chapelskip_getstudentsummaryjson.json"));
        QCOMPARE(s.remaining, 16);
        QVERIFY_THROWS_EXCEPTION(ParseError, parseBody("<html>"));
    }
};

CEDARVIEW_TEST_MAIN(TestChapelProvider, QCoreApplication)
#include "tst_chapel_provider.moc"
