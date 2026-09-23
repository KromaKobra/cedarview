// Dining menus — "what's at Home Cooking today".
//
// **This one is fully known.** The endpoint, its parameters and its response
// shape were all verified against the live service on 2026-09-16, and a real
// capture is committed at `tests/fixtures/diningdata_cedarville_edu_api_menus.json`.
//
// The endpoint
// ------------
//
//     GET https://diningdata.cedarville.edu/api/menus?days=N
//
// Unauthenticated — `diningdata.cedarville.edu`'s own page fetches it with
// `credentials: "omit"`, so there is no session to have and nothing to log
// into. That is why TransportRouter sends this origin to HttpTransport rather
// than through the WebView.
//
// Response shape, verified:
//
//     {
//       "2026-09-16": [
//         {
//           "venue": "Home Cooking",
//           "meal":  "Breakfast",
//           "slot":  "breakfast",
//           "items": [
//             {"name": "Chorizo Sausage Patties", "allergens": []},
//             {"name": "Shredded Cheese",
//              "allergens": [{"url": "…/hasDairy.png", "alt": "dairy"}]}
//           ]
//         },
//         …
//       ],
//       "2026-09-17": [ … ]
//     }
//
// About twenty blocks per day. `venue` is the station — observed values include
// Home Cooking, Grille, Italian, Habanero, SubZone, Salad & Soup, Bake Shoppe,
// Allergen Aware, Garden Bites, Power Bar, Self Cook, Breakfast All Day. `meal`
// is "Breakfast"/"Lunch"/"Dinner" for stations tied to a sitting, and null for
// all-day stations, which carry `slot: "anytime"` instead.
//
// `days=N` was tested at 1, 7 and 14 and returns exactly N dates starting
// today. The server caps it at 31: `days=60`, `120` and `365` all come back
// with 31 dates (about half a megabyte).
//
// `start=YYYY-MM-DD` moves the first date, in either direction. The site's own
// script never sends it, but it was verified on 2026-09-22 against dates from
// 2025-09-01 to 2026-11-15, all of which returned real menus. It is what lets
// the Chucks tab page back to last week or forward past the 31-day cap.
//
// A date with nothing posted — a holiday, a break, or simply too far out — is
// not an error. It comes back HTTP 200 with a single placeholder block:
//
//     {"2026-12-25": [{"venue": "No Venues Found", "meal": null,
//                      "slot": "anytime", "items": []}]}
//
// which parses like any other block and is dropped by DayMenu::forVenue(),
// leaving an empty day. There is also a `refresh=1` parameter, which the site's
// own script uses hourly; this provider never sends it, because it appears to
// force an upstream refetch from Pioneer College Caterers (`my.pcconline.com`)
// and there is no reason for a personal app to make someone else's server work
// harder.

#pragma once

#include "../models.h"
#include "../transport.h"

#include <QJsonValue>
#include <QTime>

#include <optional>
#include <utility>

namespace mycu {

// Path including the origin — this provider does not live on Self-Service, and
// TransportRouter reads the origin to pick a transport.
inline const QString DINING_PATH = DINING_BASE + QStringLiteral("/api/menus");

// How far ahead to ask for. One week: enough for "what's for dinner on Friday"
// without pulling a quarter-megabyte of JSON onto a phone.
inline constexpr int DEFAULT_DAYS = 7;

// Menus for `days` days from `start` (default today), oldest first.
class DiningProvider
{
public:
    static inline const QString label = QStringLiteral("Dining");

    // An invalid `start` leaves it to the server, which means today.
    explicit DiningProvider(TransportPtr transport, int days = DEFAULT_DAYS, QDate start = QDate());

    int days() const { return m_days; }
    QDate start() const { return m_start; }
    QString path() const;

    QList<DayMenu> fetch();
    static QList<DayMenu> parse(const Response &response);

private:
    TransportPtr m_transport;
    int m_days;
    QDate m_start;
};

// Turn the API's `{date: [block]}` mapping into DayMenu values.
//
// Sorted by date, because object ordering is a JSON serialisation detail and
// not something a UI should depend on.
//
// Individual malformed blocks are skipped with a warning rather than taken as
// fatal: a menu missing one station is still a useful menu, and this endpoint
// is ultimately reformatting someone else's data.
QList<DayMenu> parseMenus(const QJsonValue &payload);

// Home Cooking's breakfast, lunch and dinner for one date.
//
// Defaults to today. Empty if that date is not in the payload — which happens
// legitimately: the payload covers only the window that was asked for.
QList<MenuBlock> homeCookingFor(const QList<DayMenu> &days, QDate on = QDate::currentDate());

using ServingHours = std::pair<QTime, QTime>;

// (start, end) for `slot` on `on`'s day of the week.
//
// Nothing for anything that is not a sitting — in practice "anytime", the
// all-day stations, which have no window to be before or after.
//
// **Hardcoded, not the API's.** The menu feed carries no serving times at all —
// only a `slot` label — so these are copied off Chuck's posted hours. A sitting
// stops being "next" the moment its window closes: at 9:30 on a weekday the
// summary card moves on to lunch. If Chuck's changes its hours, the table in
// dining.cpp is the one place to change.
std::optional<ServingHours> servingHours(QDate on, const QString &slot);

// "10:30am–2:30pm" — a serving window, for the summary card.
QString formatHours(QTime start, QTime end);

// The sitting a reader is most likely about to eat, with its date.
//
// Walks forward from `now`: the first sitting today that has not finished
// serving, and failing that the first sitting on the next day in the payload.
// After dinner this means tomorrow's breakfast, which is the honest answer —
// the alternative is showing a menu for a meal that is already over.
//
// Nothing when nothing is left in the payload at all. Empty blocks are skipped
// rather than returned: a heading with no dishes under it reads as a bug, and
// the next real sitting is the useful answer.
//
// `now` is a parameter because the whole behaviour is a function of the clock,
// and a test that could only be run before 9:30am would be no test.
std::optional<std::pair<QDate, MenuBlock>> nextMealBlock(
    const QList<DayMenu> &days, const QDateTime &now = QDateTime::currentDateTime(),
    const QString &venue = HOME_COOKING);

} // namespace mycu
