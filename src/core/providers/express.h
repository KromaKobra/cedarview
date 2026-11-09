// The Commons Express menu — what the evening grab-and-go counter sells.
//
// It is not in the dining feed (`diningdata.cedarville.edu`, see dining.h):
// checked 2026-11-09, that feed carries The Commons' twelve stations and
// nothing else. The Commons Express lives only on its ordering site, an
// OnCampusPOS storefront run for Pioneer College Caterers:
//
//     https://cedarville.oncampuspos.com/restaurant/the-commons-express
//
// The endpoint
// ------------
//
//     GET https://cedarville.oncampuspos.com/api/frontend/item-category/items/the-commons-express
//         ?slug=the-commons-express&order_column=sort&order_type=asc
//
// Exactly what the storefront's own page sends (its `fetchCategoryWiseItems`),
// parameters included. No sign-in — the page fetches it for visitors who have
// not logged in — but every request carries an `x-api-key` header. The key is
// not a secret: it is written into the storefront's served HTML
// (`apiKey = "…"`), the same for every visitor. It is the storefront's, not
// ours, and nothing promises it will not change; if it does, this fetch fails
// with HTTP 401/403 and the page falls back to the Order button.
//
// Response shape, captured 2026-11-09 and committed at
// `tests/fixtures/cedarville_oncampuspos_com_api_frontend_item_category_items_the_commons_express.json`:
//
//     {"data": [
//       {"id": 1, "name": "Pizza by the Slice", "slug": "…",
//        "items": [{"id": 6, "name": "Pepperoni Slice",
//                   "flat_price": "1.00", "currency_price": "$1.00",
//                   "description": "<p>Single slice of pepperoni pizza</p>",
//                   "description_alt": "Single slice of pepperoni pizza",
//                   "discount": "0.00", "flat_discounted_price": "0.00",
//                   "currency_discounted_price": "$0.00",
//                   "caution": "", "thumb": "…", "cover": "…", …}]},
//       …
//     ]}
//
// Categories come back in the storefront's own order (`order_column=sort`),
// which is kept. A category can be empty (Desserts was, on the day of the
// capture); it is dropped, because a heading with nothing under it reads as a
// bug. `description_alt` is the plain-text description, but still carries
// HTML entities (`&amp;`), which are decoded.
//
// Opening hours
// -------------
//
// On the storefront's restaurant record, a second request:
//
//     GET https://cedarville.oncampuspos.com/api/frontend/restaurant/show/the-commons-express
//
//     {"data": {…, "time_slots": [{"opening_time": "08:00 PM", "closing_time": "11:00 PM",
//                                  "day": 3}, …],
//               "closed_dates": [], …}}
//
// `day` is 0 = Sunday … 6 = Saturday (the storefront's dayEnum). A day with no
// slot is closed; a day can have more than one (Wednesday had 1–4 PM as well
// as 8–11 PM on 2026-11-09). The record also carries the storefront's
// reviews, phone and email, none of which is wanted, so only `time_slots` is
// kept, and that is all the cache and the fixture hold. `closed_dates` was
// empty when captured and its entries' shape is unknown, so it is not read.
//
// The hours are a second, lesser fetch: if it fails, the menu still loads and
// the hours fall back to the hand-entered ones (core/hours.h), which come from
// Cedarville's dining page.

#pragma once

#include "../hours.h"
#include "../transport.h"

#include <QJsonValue>
#include <QList>
#include <QString>

#include <array>

namespace mycu {

inline const QString ORDERING_BASE = QStringLiteral("https://cedarville.oncampuspos.com");

inline const QString EXPRESS_SLUG = QStringLiteral("the-commons-express");

// Where the Order button goes: the storefront itself.
inline const QString EXPRESS_ORDER_URL = ORDERING_BASE + QStringLiteral("/restaurant/") + EXPRESS_SLUG;

inline const QString EXPRESS_RESTAURANT_PATH = ORDERING_BASE + QStringLiteral("/api/frontend/restaurant/show/")
    + EXPRESS_SLUG;

inline const QString EXPRESS_PATH = ORDERING_BASE + QStringLiteral("/api/frontend/item-category/items/")
    + EXPRESS_SLUG + QStringLiteral("?slug=") + EXPRESS_SLUG + QStringLiteral("&order_column=sort&order_type=asc");

// The storefront's public API key, as served in its own page. See above.
inline const QByteArray ORDERING_API_KEY = QByteArrayLiteral("h1c86fn1-h3x4-40g5-5596-q539a02383k306031");

struct ExpressItem
{
    QString name;
    // As the storefront formats it: "$3.25". The discounted price when there
    // is a discount.
    QString price;
    QString description;

    bool operator==(const ExpressItem &) const = default;
};

struct ExpressCategory
{
    QString name;
    QList<ExpressItem> items;

    bool operator==(const ExpressCategory &) const = default;
};

// The storefront's weekly hours. Empty when they could not be fetched.
struct ExpressHours
{
    // By Qt's day of the week, Monday first: byWeekday[day.dayOfWeek() - 1].
    std::array<hours::DayWindows, 7> byWeekday;

    bool isEmpty() const;
    hours::DayWindows on(QDate day) const { return byWeekday[day.dayOfWeek() - 1]; }

    bool operator==(const ExpressHours &) const = default;
};

struct ExpressMenu
{
    QList<ExpressCategory> categories;
    ExpressHours hours;

    bool operator==(const ExpressMenu &) const = default;
};

class ExpressProvider
{
public:
    static inline const QString label = QStringLiteral("The Commons Express");

    explicit ExpressProvider(TransportPtr transport);

    // `{"menu": <the menu as it arrived>, "restaurant": {"time_slots": […]}}` —
    // what the on-device cache keeps. "restaurant" is null when the hours
    // could not be fetched; the menu failing fails the whole fetch.
    QJsonValue fetchPayload();

    ExpressMenu fetch();

private:
    TransportPtr m_transport;
};

// The whole of what fetchPayload() returns.
ExpressMenu parseExpress(const QJsonValue &payload);

// The categories with at least one item, in the storefront's order. Malformed
// categories and items are skipped with a warning; a payload with none usable
// at all is a ParseError.
QList<ExpressCategory> parseExpressMenu(const QJsonValue &payload);

// The hours from the restaurant record — the whole `{"data": {…}}` response
// or the trimmed `{"time_slots": […]}` the cache keeps. Unreadable slots are
// skipped; nothing readable at all is empty hours, not an error.
ExpressHours parseExpressHours(const QJsonValue &payload);

} // namespace mycu
