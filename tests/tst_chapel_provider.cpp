// Chapel skips, against REAL captured responses.
//
// Fixtures are verbatim captures from a signed-in session on 2026-09-17, with
// the student ID scrubbed to 1234567 and the name to "Sample Student".
// Structure, counts and vocabulary are untouched — those are the thing being
// tested.

#include "testsupport.h"

#include <condition_variable>
#include <mutex>

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
// Records what was asked for. Thread-safe: the provider asks for the three
// JSON endpoints at once.
class Recording : public Transport
{
public:
    std::function<Response(const QString &)> handler;
    QStringList seen;

    Response get(const QString &path) override
    {
        {
            const std::lock_guard lock(mutex);
            seen.append(path);
        }
        if (handler)
            return handler(path);
        return inner.get(path);
    }

    int count(const QString &fragment)
    {
        const std::lock_guard lock(mutex);
        return static_cast<int>(std::count_if(seen.cbegin(), seen.cend(),
                                              [&](const QString &p) { return p.contains(fragment); }));
    }

    std::mutex mutex;
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

    // Four requests the first time: the page for the ID, then summary, ledger
    // and fines (in no particular order — they go at once).
    void providerRequestsTheDashboardThenTheJson()
    {
        auto recording = std::make_shared<Recording>();
        ChapelProvider provider(recording);
        provider.fetch();

        QCOMPARE(recording->seen[0], CHAPEL_PATH);
        QCOMPARE(recording->seen.size(), 4);
        for (const QString &endpoint : {SUMMARY_PATH, LEDGER_PATH, FINES_PATH})
            QCOMPARE(recording->count(endpoint + "?studentId=1234567"), 1);
        QCOMPARE(provider.knownStudentId(), QStringLiteral("1234567"));
    }

    // A remembered ID makes a refresh three requests, not four.
    void aKnownStudentIdSkipsTheDashboard()
    {
        auto recording = std::make_shared<Recording>();
        const ChapelSummary s = ChapelProvider(recording, "1234567").fetch();
        QCOMPARE(s.remaining, 16);
        QVERIFY(!recording->seen.contains(CHAPEL_PATH));
        QCOMPARE(recording->seen.size(), 3);
    }

    // A remembered ID that the server no longer answers for is re-read off the
    // dashboard, and the JSON asked for once more.
    void aStaleStudentIdFallsBackOnce()
    {
        auto recording = std::make_shared<Recording>();
        recording->handler = [&](const QString &path) -> Response {
            if (path.contains("studentId=9999999"))
                throw TransportError(path + " returned HTTP 404", 404);
            return recording->inner.get(path);
        };
        ChapelProvider provider(recording, "9999999");
        const ChapelSummary s = provider.fetch();
        QCOMPARE(s.remaining, 16);
        QCOMPARE(provider.knownStudentId(), QStringLiteral("1234567"));
        QCOMPARE(recording->seen.count(CHAPEL_PATH), 1);
        QCOMPARE(recording->count("studentId=9999999"), 3);
        QCOMPARE(recording->count("studentId=1234567"), 3);
    }

    // When the dashboard names the same student, the ID was not the problem:
    // the error stands, rather than a second identical round.
    void aServerErrorIsNotRetriedAsAStaleId()
    {
        auto recording = std::make_shared<Recording>();
        recording->handler = [&](const QString &path) -> Response {
            if (path.contains("Summary"))
                throw TransportError(path + " returned HTTP 403", 403);
            return recording->inner.get(path);
        };
        QVERIFY_THROWS_EXCEPTION(TransportError, ChapelProvider(recording, "1234567").fetch());
        QCOMPARE(recording->count("Summary"), 1);
    }

    // Offline is not a stale ID; nothing is re-read.
    void aNetworkFailureIsNotRetried()
    {
        auto recording = std::make_shared<Recording>();
        recording->handler = [](const QString &path) -> Response {
            throw TransportError("could not reach " + path);
        };
        QVERIFY_THROWS_EXCEPTION(TransportError, ChapelProvider(recording, "1234567").fetch());
        QVERIFY(!recording->seen.contains(CHAPEL_PATH));
    }

    // The three JSON requests are in flight together: each one here waits
    // until all three have arrived, which only works if they run at once.
    void theThreeRequestsRunConcurrently()
    {
        auto recording = std::make_shared<Recording>();
        std::mutex mutex;
        std::condition_variable arrived;
        int waiting = 0;
        recording->handler = [&](const QString &path) -> Response {
            std::unique_lock lock(mutex);
            ++waiting;
            arrived.notify_all();
            if (!arrived.wait_for(lock, std::chrono::seconds(5), [&] { return waiting >= 3; }))
                throw TransportError(QStringLiteral("the requests were made one at a time"));
            lock.unlock();
            return recording->inner.get(path);
        };
        QCOMPARE(ChapelProvider(recording, "1234567").fetch().remaining, 16);
    }

    void thePayloadKeepsAllThreeAsTheyArrived()
    {
        const QJsonObject payload =
            ChapelProvider(std::make_shared<FixtureTransport>(testing::fixturesDir())).fetchPayload();
        QCOMPARE(payload.keys(), (QStringList{"fines", "ledger", "summary"}));
        QCOMPARE(payload.value("summary").toObject().value("SkipsRemaining").toInt(), 16);
        QCOMPARE(buildSummary(payload).remaining, 16);
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
