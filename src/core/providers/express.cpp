#include "express.h"

#include "../htmlattrs.h"
#include "../log.h"
#include "../pyjson.h"

#include <QJsonArray>
#include <QJsonObject>

#include <QTime>

#include <algorithm>
#include <optional>

namespace mycu {

namespace {

QString text(const QJsonObject &obj, const char *key)
{
    return html::decodeEntities(json::clean(obj.value(QString::fromLatin1(key)))).simplified();
}

// "0.00" and missing are both no discount.
bool discounted(const QJsonObject &obj)
{
    const double flat = json::clean(obj.value(QStringLiteral("flat_price"))).toDouble();
    const double reduced = json::clean(obj.value(QStringLiteral("flat_discounted_price"))).toDouble();
    return reduced > 0 && reduced < flat;
}

std::optional<ExpressItem> parseItem(const QJsonValue &raw)
{
    if (!raw.isObject())
        return std::nullopt;
    const QJsonObject obj = raw.toObject();

    ExpressItem item;
    item.name = text(obj, "name");
    if (item.name.isEmpty())
        return std::nullopt;
    item.price = text(obj, discounted(obj) ? "currency_discounted_price" : "currency_price");
    item.description = text(obj, "description_alt");
    return item;
}

// "08:00 PM" -> 1200.
std::optional<int> minuteOf(const QString &text)
{
    for (const QString &format : {QStringLiteral("hh:mm AP"), QStringLiteral("h:mm AP")}) {
        const QTime time = QTime::fromString(text.toUpper(), format);
        if (time.isValid())
            return time.hour() * 60 + time.minute();
    }
    return std::nullopt;
}

} // namespace

ExpressProvider::ExpressProvider(TransportPtr transport)
    : m_transport(std::move(transport))
{}

QJsonValue ExpressProvider::fetchPayload()
{
    const QJsonValue menu = m_transport->get(EXPRESS_PATH).raiseForSession().json();

    QJsonValue restaurant = QJsonValue::Null;
    try {
        const QJsonObject record =
            m_transport->get(EXPRESS_RESTAURANT_PATH).json().toObject().value(QStringLiteral("data")).toObject();
        restaurant = QJsonObject{{QStringLiteral("time_slots"), record.value(QStringLiteral("time_slots"))}};
    } catch (const std::exception &e) {
        qCWarning(lcDining) << "express: no hours from the storefront (using the posted ones):" << e.what();
    }
    return QJsonObject{{QStringLiteral("menu"), menu}, {QStringLiteral("restaurant"), restaurant}};
}

ExpressMenu ExpressProvider::fetch()
{
    return parseExpress(fetchPayload());
}

ExpressMenu parseExpress(const QJsonValue &payload)
{
    const QJsonObject obj = payload.toObject();
    return {parseExpressMenu(obj.value(QStringLiteral("menu"))), parseExpressHours(obj.value(QStringLiteral("restaurant")))};
}

bool ExpressHours::isEmpty() const
{
    return std::all_of(byWeekday.cbegin(), byWeekday.cend(), [](const hours::DayWindows &d) { return d.isEmpty(); });
}

ExpressHours parseExpressHours(const QJsonValue &payload)
{
    QJsonObject record = payload.toObject();
    if (record.value(QStringLiteral("data")).isObject())
        record = record.value(QStringLiteral("data")).toObject();

    ExpressHours hours;
    for (const QJsonValue &raw : record.value(QStringLiteral("time_slots")).toArray()) {
        const QJsonObject slot = raw.toObject();
        const QJsonValue day = slot.value(QStringLiteral("day"));
        const std::optional<int> open = minuteOf(json::clean(slot.value(QStringLiteral("opening_time"))));
        std::optional<int> close = minuteOf(json::clean(slot.value(QStringLiteral("closing_time"))));
        if (!day.isDouble() || day.toInt() < 0 || day.toInt() > 6 || !open || !close) {
            qCWarning(lcDining) << "express: skipping an unreadable time slot";
            continue;
        }
        // Closing at or before opening runs past midnight (hours.h's rule).
        if (*close <= *open)
            *close += 24 * 60;
        // The storefront's 0 is Sunday; Qt's is 7.
        const int weekday = day.toInt() == 0 ? 7 : day.toInt();
        hours.byWeekday[weekday - 1].append(hours::Window{*open, *close});
    }
    for (hours::DayWindows &windows : hours.byWeekday) {
        std::sort(windows.begin(), windows.end(),
                  [](const hours::Window &a, const hours::Window &b) { return a.open < b.open; });
    }
    return hours;
}

QList<ExpressCategory> parseExpressMenu(const QJsonValue &payload)
{
    const QJsonValue data = payload.toObject().value(QStringLiteral("data"));
    if (!data.isArray()) {
        throw ParseError(QStringLiteral("expected {\"data\": [categories]} from the Commons Express menu, got %1")
                             .arg(json::typeName(payload.isObject() ? data : payload)));
    }

    QList<ExpressCategory> categories;
    for (const QJsonValue &raw : data.toArray()) {
        if (!raw.isObject()) {
            qCWarning(lcDining) << "express: skipping a category that is not an object";
            continue;
        }
        const QJsonObject obj = raw.toObject();
        ExpressCategory category{text(obj, "name"), {}};
        for (const QJsonValue &rawItem : obj.value(QStringLiteral("items")).toArray()) {
            if (auto item = parseItem(rawItem))
                category.items.append(*item);
        }
        if (category.name.isEmpty() || category.items.isEmpty())
            continue;
        categories.append(category);
    }

    if (categories.isEmpty())
        throw ParseError(QStringLiteral("the Commons Express menu had no items"));
    return categories;
}

} // namespace mycu
