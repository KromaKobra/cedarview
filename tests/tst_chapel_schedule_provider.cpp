// Upcoming chapels, against a REAL captured response.
//
// Fixture: `mediaserve_cedarville_edu_chapelmedia_api_v2_chapels_upcoming.json`,
// a verbatim capture of the live v2 API taken 2026-09-17. No personal data —
// this is the public chapel schedule, and the endpoint needs no authentication.

#include "testsupport.h"

#include "core/providers/chapel_schedule.h"

#include <QTimeZone>
#include <QUrlQuery>

using namespace mycu;

namespace {

const char *FIXTURE = "mediaserve_cedarville_edu_chapelmedia_api_v2_chapels_upcoming.json";

QList<UpcomingChapel> upcoming()
{
    return parseUpcoming(testing::fixtureJson(FIXTURE));
}

QJsonValue parse(const char *text)
{
    const QJsonDocument doc = QJsonDocument::fromJson(text);
    return doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
}

QDateTime at(double hours)
{
    return QDateTime::currentDateTime().addSecs(qint64(hours * 3600));
}

UpcomingChapel chapelAt(QDateTime when, const QString &title = QString())
{
    UpcomingChapel c;
    c.startsAt = when;
    c.title = title;
    return c;
}

// Serves pages[n-1] for page=n; an empty page past the end.
class PagedTransport : public Transport
{
public:
    explicit PagedTransport(QList<QJsonArray> pages) : pages(std::move(pages)) {}

    Response get(const QString &path) override
    {
        const int page = QUrlQuery(QUrl(path)).queryItemValue("page").toInt();
        asked.append(page);
        const QJsonArray items = page <= pages.size() ? pages[page - 1] : QJsonArray();
        return Response{200, path, testing::toJson(QJsonObject{{"Items", items}}), {}};
    }

    QList<QJsonArray> pages;
    QList<int> asked;
};

QJsonObject item(int day, const QString &title = "Speaker")
{
    return QJsonObject{
        {"Date", QStringLiteral("2026-10-%1T14:00:00Z").arg(day, 2, 10, QLatin1Char('0'))},
        {"Title", QStringLiteral("%1 %2").arg(title).arg(day)},
        {"Speakers", QJsonArray()},
        {"WillLiveStream", true},
    };
}

QStringList titles(const QList<UpcomingChapel> &chapels)
{
    QStringList out;
    for (const auto &c : chapels)
        out.append(c.title);
    return out;
}

const QDateTime START(QDate(2026, 9, 23), QTime(14, 0), QTimeZone::UTC);

} // namespace

class TestChapelScheduleProvider : public QObject
{
    Q_OBJECT

private slots:
    // ---- Shape ---------------------------------------------------------------

    void theCaptureHasAFullPage() { QCOMPARE(upcoming().size(), 20); }

    void theFirstEntryIsTheRealOne()
    {
        const UpcomingChapel first = upcoming().first();
        QCOMPARE(first.speakers, QStringList{"Garrett Higbee"});
        QCOMPARE(first.willLivestream, true);
        QVERIFY(first.startsAt.isValid());
        // 14:00Z is 10:00 Eastern; asserted in UTC so the test does not depend
        // on the machine's timezone.
        QCOMPARE(first.startsAt.toUTC(), QDateTime(QDate(2026, 9, 17), QTime(14, 0), QTimeZone::UTC));
    }

    void entriesAreSortedSoonestFirst()
    {
        const auto chapels = upcoming();
        for (qsizetype i = 1; i < chapels.size(); ++i)
            QVERIFY(chapels[i - 1].startsAt <= chapels[i].startsAt);
    }

    // Converted to local time for display, but still the same instant — a
    // naive reading here would silently shift every chapel by the viewer's
    // UTC offset.
    void datesAreLocalTimeForTheRightInstant()
    {
        for (const auto &c : upcoming()) {
            QCOMPARE(c.startsAt.timeSpec(), Qt::LocalTime);
            QCOMPARE(c.startsAt.toUTC().time().minute(), 0);
        }
    }

    // ---- The two things the real data teaches -------------------------------

    // "Worship Chapel" and "SGA" are real entries with `Speakers: []`.
    //
    // Anything that assumes `Speakers[0]` exists crashes on the *second* item
    // in the live feed.
    void someChapelsGenuinelyHaveNoSpeaker()
    {
        bool worship = false;
        int speakerless = 0;
        for (const auto &c : upcoming()) {
            if (c.speakers.isEmpty()) {
                ++speakerless;
                worship = worship || c.title == "Worship Chapel";
            }
        }
        QVERIFY2(speakerless > 0, "the capture should contain speakerless chapels");
        QVERIFY(worship);
    }

    void whoFallsBackToTheEventName()
    {
        for (const auto &c : upcoming()) {
            if (c.title == "Worship Chapel") {
                QVERIFY(c.speakers.isEmpty());
                QCOMPARE(c.who(), QStringLiteral("Worship Chapel"));
                return;
            }
        }
        QFAIL("no Worship Chapel in the capture");
    }

    void whoNeverReturnsAnEmptyString() { QCOMPARE(UpcomingChapel().who(), QStringLiteral("Chapel")); }

    // The API sets Title to the speaker's name, so "X — X" must be avoidable.
    void titleDuplicatingTheSpeakerIsDetectable()
    {
        const UpcomingChapel higbee = upcoming().first();
        QCOMPARE(higbee.title, QStringLiteral("Garrett Higbee"));
        QCOMPARE(higbee.isSameAsTitle(), true);
    }

    // Regression, seen on the phone: "Worship Chapel" above "Worship Chapel".
    //
    // This assertion used to read `is_same_as_title is False` and was wrong —
    // it encoded the property's old question, "does the title repeat the
    // *speakers*?", which is trivially false when there are no speakers. But a
    // chapel with no named speaker is precisely when who() falls back to the
    // title, so the screen rendered it twice. The question that matters is
    // "does the title repeat what we are already showing?".
    void anUnnamedChapelDoesNotPrintItsOwnNameTwice()
    {
        for (const auto &c : upcoming()) {
            if (c.title == "Worship Chapel") {
                QVERIFY(c.speakers.isEmpty());
                QCOMPARE(c.who(), QStringLiteral("Worship Chapel"));
                QCOMPARE(c.isSameAsTitle(), true);
                return;
            }
        }
        QFAIL("no Worship Chapel in the capture");
    }

    // ---- nextChapel ----------------------------------------------------------

    // The feed keeps today's chapel listed after it has begun.
    void nextChapelSkipsOneThatAlreadyStarted()
    {
        const UpcomingChapel past = chapelAt(at(-2), "Already happened");
        const UpcomingChapel future = chapelAt(at(2), "Coming up");
        QCOMPARE(nextChapel({past, future}), future);
    }

    // Legitimate over the summer; must not be an error.
    void nextChapelIsNothingWhenNothingIsUpcoming()
    {
        QVERIFY(!nextChapel({}));
        QVERIFY(!nextChapel({chapelAt(at(-1))}));
    }

    void nextChapelIgnoresUndatedEntries() { QVERIFY(!nextChapel({UpcomingChapel()})); }

    // ---- Robustness ----------------------------------------------------------

    void anEmptyScheduleIsNotAnError()
    {
        QVERIFY(parseUpcoming(parse(R"({"Items": [], "TotalCount": 0})")).isEmpty());
    }

    void aMissingItemsKeyThrowsWithTheKeysItDidSee()
    {
        CV_VERIFY_THROWS_MATCHING(parseUpcoming(parse(R"({"TotalCount": 0})")), ParseError, "Items");
        CV_VERIFY_THROWS_MATCHING(parseUpcoming(parse(R"({"TotalCount": 0})")), ParseError, "'TotalCount'");
    }

    void aNonObjectPayloadThrows()
    {
        QVERIFY_THROWS_EXCEPTION(ParseError, parseUpcoming(parse(R"([{"Title": "x"}])")));
    }

    void aMalformedEntryIsSkippedNotFatal()
    {
        const auto chapels = parseUpcoming(
            parse(R"({"Items": ["nonsense", {"Title": "Real", "Date": "2026-09-17T14:00:00Z"}]})"));
        QCOMPARE(chapels.size(), 1);
        QCOMPARE(chapels[0].title, QStringLiteral("Real"));
    }

    void anUnparseableDateCostsOnlyThatEntry()
    {
        const auto chapels = parseUpcoming(parse(R"({"Items": [
            {"Title": "Bad date", "Date": "next Tuesday-ish"},
            {"Title": "Good", "Date": "2026-09-17T14:00:00Z"}]})"));
        QCOMPARE(chapels.size(), 2);
        // Undated entries sort last rather than breaking the comparison.
        QCOMPARE(chapels.first().title, QStringLiteral("Good"));
        QVERIFY(!chapels.last().startsAt.isValid());
    }

    // Guessing local would shift every time by the viewer's offset.
    void aDateWithoutAZoneIsAssumedUtc()
    {
        const auto chapels = parseUpcoming(parse(R"({"Items": [{"Date": "2026-09-17T14:00:00"}]})"));
        QCOMPARE(chapels[0].startsAt.toUTC().time().hour(), 14);
    }

    void anExplicitOffsetIsHonoured()
    {
        const auto chapels = parseUpcoming(parse(R"({"Items": [{"Date": "2026-09-17T10:00:00-04:00"}]})"));
        QCOMPARE(chapels[0].startsAt.toUTC().time().hour(), 14);
    }

    void blankSpeakerEntriesAreDropped()
    {
        const auto chapels = parseUpcoming(parse(R"({"Items": [{"Speakers": ["  ", "", "Real Person"]}]})"));
        QCOMPARE(chapels[0].speakers, QStringList{"Real Person"});
    }

    // ---- The provider --------------------------------------------------------

    void providerTargetsTheMediaApi()
    {
        QVERIFY(ChapelScheduleProvider(nullptr).path().startsWith(UPCOMING_PATH));
    }

    void providerRequestsTheCountItWasGiven()
    {
        QVERIFY(ChapelScheduleProvider(nullptr, 5).path().endsWith("count=5"));
    }

    void countIsClamped() { QCOMPARE(ChapelScheduleProvider(nullptr, 0).count(), 1); }

    void providerEndToEndOverTheFixture()
    {
        const auto chapels =
            ChapelScheduleProvider(std::make_shared<FixtureTransport>(testing::fixturesDir())).fetch();
        QCOMPARE(chapels.size(), 20);
        QCOMPARE(chapels[0].who(), QStringLiteral("Garrett Higbee"));
    }

    void providerAsksForPageOneUnlessToldOtherwise()
    {
        QVERIFY(ChapelScheduleProvider(nullptr).path().contains("?page=1&"));
        QVERIFY(ChapelScheduleProvider(nullptr, DEFAULT_COUNT, 3).path().contains("?page=3&"));
        QCOMPARE(ChapelScheduleProvider(nullptr, DEFAULT_COUNT, 0).page(), 1);
    }

    // ---- The whole feed, across pages ---------------------------------------

    void fetchScheduleWalksPagesUntilAShortOne()
    {
        auto transport = std::make_shared<PagedTransport>(
            QList<QJsonArray>{{item(1), item(2)}, {item(3)}});
        QCOMPARE(titles(fetchSchedule(transport, 2)),
                 (QStringList{"Speaker 1", "Speaker 2", "Speaker 3"}));
        QCOMPARE(transport->asked, (QList<int>{1, 2}));
    }

    void aFullLastPageCostsOneEmptyRequest()
    {
        auto transport = std::make_shared<PagedTransport>(QList<QJsonArray>{{item(1), item(2)}});
        QCOMPARE(fetchSchedule(transport, 2).size(), 2);
        QCOMPARE(transport->asked, (QList<int>{1, 2}));
    }

    void fetchScheduleGivesUpAfterMaxPages()
    {
        QList<QJsonArray> pages;
        for (int d = 1; d < 20; ++d)
            pages.append(QJsonArray{item(d)});
        auto transport = std::make_shared<PagedTransport>(pages);
        QCOMPARE(fetchSchedule(transport, 1, 3).size(), 3);
        QCOMPARE(transport->asked, (QList<int>{1, 2, 3}));
    }

    void anEmptyFeedIsAnEmptySchedule()
    {
        QVERIFY(fetchSchedule(std::make_shared<PagedTransport>(QList<QJsonArray>{})).isEmpty());
    }

    // If a chapel starts between the two requests, page 2 shifts up a slot.
    void aChapelRepeatedAcrossPagesIsListedOnce()
    {
        auto transport = std::make_shared<PagedTransport>(
            QList<QJsonArray>{{item(1), item(2)}, {item(2)}});
        QCOMPARE(titles(fetchSchedule(transport, 2)), (QStringList{"Speaker 1", "Speaker 2"}));
    }

    // The capture is a 20-item page — shorter than 30, so the feed has ended.
    void fetchScheduleOverTheFixtureIsOneRequest()
    {
        QCOMPARE(fetchSchedule(std::make_shared<FixtureTransport>(testing::fixturesDir())).size(), 20);
    }

    // ---- Now and over --------------------------------------------------------

    void aChapelIsHappeningForItsLengthAndThenOver()
    {
        const UpcomingChapel chapel = chapelAt(START);
        QVERIFY(!isHappening(chapel, START.addSecs(-60)));
        QVERIFY(isHappening(chapel, START));
        QVERIFY(isHappening(chapel, START.addSecs(CHAPEL_LENGTH_SECS - 1)));
        QVERIFY(!isHappening(chapel, START.addSecs(CHAPEL_LENGTH_SECS)));
        QVERIFY(isOver(chapel, START.addSecs(CHAPEL_LENGTH_SECS)));
        QVERIFY(!isOver(chapel, START));
    }

    void anUndatedChapelIsNeitherHappeningNorOver()
    {
        const UpcomingChapel chapel;
        QVERIFY(!isHappening(chapel, START));
        QVERIFY(!isOver(chapel, START));
    }
};

CEDARVIEW_TEST_MAIN(TestChapelScheduleProvider, QCoreApplication)
#include "tst_chapel_schedule_provider.moc"
