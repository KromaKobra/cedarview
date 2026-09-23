// Upcoming chapels — who is speaking next.
//
// **Verified, unauthenticated, and found rather than guessed.**
//
// `cedarville.edu/chapel` renders its "Upcoming" tab client-side, so the
// schedule is nowhere in the served HTML — which is why searching the page for
// it failed. Running `scripts/discover chapel-schedule` read the page's own
// resource timing log and turned up the API it actually calls:
//
//     GET https://mediaserve.cedarville.edu/ChapelMedia/api/v2/chapels/upcoming
//         ?page=1&count=N
//
// No auth, no cookies, no session. Response, verified 2026-09-17:
//
//     {
//       "TotalCount": 51, "Page": 1, "RequestedCount": 20, "RetrievedCount": 20,
//       "Items": [
//         {"Id": "ICiRTos5BUiwi_xQP8LHyg",
//          "Title": "Garrett Higbee",
//          "Date": "2026-09-17T14:00:00Z",
//          "Description": "…",
//          "YouTubeId": "ySIBhMll7k0",
//          "PreviewUrl": "…",
//          "Speakers": ["Garrett Higbee"],
//          "WillLiveStream": true}
//       ]
//     }
//
// `count` is capped at 30 by the server (`count=100` answers with
// `RequestedCount: 30`), so the whole schedule takes more than one page: on
// 2026-09-22 `TotalCount` was 47, page 1 held 30, page 2 held 17 and page 3 was
// empty. fetchSchedule() walks the pages.
//
// Sibling endpoints on the same API, should you want them later:
// `/chapels/recent`, `/chapels/popular`, `/chapel/live`. `/chapel/live` is
// where CHAPEL_LENGTH comes from — it reports the current or next chapel with
// `StartDate`/`EndDate` (14:00Z–14:45Z), which the upcoming feed does not.
//
// Two things the real data teaches
// --------------------------------
//
// * `Speakers` is **often empty**. "Worship Chapel", "SGA" and similar are real
//   scheduled chapels with no named speaker. Anything that assumes
//   `Speakers[0]` exists will crash on the second item in the live feed.
// * `Title` is frequently the speaker's name verbatim, so rendering
//   "title — speaker" gives you "Garrett Kell — Garrett Kell".
//
// Both are handled by UpcomingChapel.
//
// Times
// -----
//
// `Date` is ISO 8601 in UTC (`14:00:00Z` = 10:00 Eastern). It is parsed as UTC
// and converted to local time for display, rather than assuming the phone is
// in Ohio.

#pragma once

#include "../models.h"
#include "../transport.h"

#include <QJsonValue>

#include <optional>

namespace mycu {

// The chapel media service. Unauthenticated, so it is routed to HttpTransport.
inline const QString CHAPEL_MEDIA_BASE = QStringLiteral("https://mediaserve.cedarville.edu");

inline const QString UPCOMING_PATH = CHAPEL_MEDIA_BASE + QStringLiteral("/ChapelMedia/api/v2/chapels/upcoming");

// How many to ask for. The feed had 51 entries when checked — a full semester.
// Ten is a fortnight or so, which is as far ahead as anyone plans chapel.
inline constexpr int DEFAULT_COUNT = 10;

// The most the server will return per page, whatever `count` asks for.
inline constexpr int PAGE_SIZE = 30;

// A ceiling on pages walked by fetchSchedule() — five pages is 150 chapels,
// more than a whole term. Only there so a feed that never returns a short page
// cannot keep the app fetching forever.
inline constexpr int MAX_PAGES = 5;

// How long a chapel lasts, in seconds. The upcoming feed carries only a start
// time; `/chapel/live` gives both ends (14:00Z–14:45Z on 2026-09-23), and this
// is that gap. It decides when a chapel is "now" and when it is over.
inline constexpr qint64 CHAPEL_LENGTH_SECS = 45 * 60;

// One page of upcoming chapels, soonest first.
class ChapelScheduleProvider
{
public:
    static inline const QString label = QStringLiteral("Chapel schedule");

    explicit ChapelScheduleProvider(TransportPtr transport, int count = DEFAULT_COUNT, int page = 1);

    QString path() const;
    int count() const { return m_count; }
    int page() const { return m_page; }

    QList<UpcomingChapel> fetch();
    static QList<UpcomingChapel> parse(const Response &response);

private:
    TransportPtr m_transport;
    int m_count;
    int m_page;
};

// Turn the API envelope into UpcomingChapel values, soonest first.
//
// An empty `Items` list is **not** an error — over the summer, or after the
// last chapel of a term, there genuinely is nothing upcoming. Returning an
// empty list lets the UI say so honestly instead of showing a failure.
QList<UpcomingChapel> parseUpcoming(const QJsonValue &payload);

// Every upcoming chapel in the feed, soonest first, across pages.
//
// Stops at the first page shorter than `pageSize`, which is how the feed says
// it has run out — two requests for a typical term. Duplicates are dropped, in
// case the feed shifts between page requests (a chapel starting between the
// two would move every later one up a slot).
QList<UpcomingChapel> fetchSchedule(const TransportPtr &transport, int pageSize = PAGE_SIZE,
                                    int maxPages = MAX_PAGES);

// The soonest chapel that has not already started.
//
// The feed keeps today's chapel listed after it has begun, so "next" has to
// mean "still in the future" rather than "first in the list".
std::optional<UpcomingChapel> nextChapel(const QList<UpcomingChapel> &chapels,
                                         const QDateTime &now = QDateTime::currentDateTime());

// Whether `chapel` has finished. Undated chapels are never over.
bool isOver(const UpcomingChapel &chapel, const QDateTime &now);

// Whether `chapel` has started and not yet finished.
bool isHappening(const UpcomingChapel &chapel, const QDateTime &now);

} // namespace mycu
