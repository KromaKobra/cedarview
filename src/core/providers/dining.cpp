#include "dining.h"

#include "../log.h"
#include "../pyjson.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace mycu {

namespace {

QDate parseIsoDate(const QString &text)
{
    static const QRegularExpression shape(QStringLiteral("^(\\d{4})-(\\d{1,2})-(\\d{1,2})$"));
    const QRegularExpressionMatch m = shape.match(text);
    if (!m.hasMatch())
        return {};
    return QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
}

std::optional<MenuItem> parseItem(const QJsonValue &raw)
{
    if (!raw.isObject())
        return std::nullopt;
    const QJsonObject obj = raw.toObject();

    MenuItem item;
    item.name = json::clean(obj.value(QStringLiteral("name")));
    if (item.name.isEmpty())
        return std::nullopt;

    // The live API sends `allergens: [{url, alt}]`. The site's own menu.js
    // still reads `tags`, which the API no longer returns — its renderer is out
    // of date with its own backend. Accept both: if `tags` ever comes back, it
    // costs one line to keep working.
    QJsonValue rawAllergens = obj.value(QStringLiteral("allergens"));
    if (rawAllergens.isUndefined() || rawAllergens.isNull())
        rawAllergens = obj.value(QStringLiteral("tags"));

    for (const QJsonValue &entry : rawAllergens.toArray()) {
        QString label;
        if (entry.isObject()) {
            const QJsonObject tag = entry.toObject();
            const QJsonValue alt = tag.value(QStringLiteral("alt"));
            label = json::clean(json::truthy(alt) ? alt : tag.value(QStringLiteral("name")));
        } else {
            label = json::clean(entry);
        }
        if (!label.isEmpty() && !item.allergens.contains(label))
            item.allergens.append(label);
    }
    return item;
}

std::optional<MenuBlock> parseBlock(const QJsonValue &raw)
{
    if (!raw.isObject())
        return std::nullopt;
    const QJsonObject obj = raw.toObject();

    MenuBlock block;
    block.venue = json::clean(obj.value(QStringLiteral("venue")));
    if (block.venue.isEmpty())
        return std::nullopt;

    // `meal` is null for all-day stations; normalised to "" so the model never
    // has to think about null, and sortKey() puts them last.
    block.meal = json::clean(obj.value(QStringLiteral("meal")));
    block.slot = json::clean(obj.value(QStringLiteral("slot")));
    for (const QJsonValue &rawItem : obj.value(QStringLiteral("items")).toArray()) {
        if (auto item = parseItem(rawItem))
            block.items.append(*item);
    }
    return block;
}

// Chuck's posted serving hours, per slot, keyed by the kind of day.
struct DayHours
{
    ServingHours breakfast, lunch, dinner;
};

const DayHours WEEKDAY{{QTime(7, 0), QTime(9, 30)},
                       {QTime(10, 30), QTime(14, 30)},
                       {QTime(16, 30), QTime(19, 30)}};
const DayHours SATURDAY{{QTime(8, 0), QTime(9, 0)},
                        {QTime(11, 0), QTime(13, 0)},
                        {QTime(16, 30), QTime(18, 30)}};
const DayHours SUNDAY{{QTime(8, 0), QTime(9, 0)},
                      {QTime(11, 30), QTime(14, 0)},
                      {QTime(17, 0), QTime(19, 30)}};

// "7am" / "9:30am" / "2:30pm".
QString clockShort(QTime at)
{
    const int hour = at.hour() % 12 == 0 ? 12 : at.hour() % 12;
    const QString minutes = at.minute() ? QStringLiteral(":%1").arg(at.minute(), 2, 10, QLatin1Char('0'))
                                        : QString();
    return QString::number(hour) + minutes + (at.hour() < 12 ? QStringLiteral("am") : QStringLiteral("pm"));
}

} // namespace

DiningProvider::DiningProvider(TransportPtr transport, int days, QDate start)
    : m_transport(std::move(transport))
    , m_days(std::max(1, days))
    , m_start(start)
{}

QString DiningProvider::path() const
{
    QString path = DINING_PATH + QStringLiteral("?days=") + QString::number(m_days);
    if (m_start.isValid())
        path += QStringLiteral("&start=") + m_start.toString(Qt::ISODate);
    return path;
}

QList<DayMenu> DiningProvider::fetch()
{
    const Response response = m_transport->get(path());
    return parse(response.raiseForSession());
}

QList<DayMenu> DiningProvider::parse(const Response &response)
{
    return parseMenus(response.json());
}

QList<DayMenu> parseMenus(const QJsonValue &payload)
{
    if (!payload.isObject()) {
        throw ParseError(QStringLiteral("expected a {date: [blocks]} object from the dining API, got %1")
                             .arg(json::typeName(payload)));
    }
    const QJsonObject obj = payload.toObject();

    QStringList keys = obj.keys();
    keys.sort();

    QList<DayMenu> days;
    for (const QString &rawDate : keys) {
        const QDate on = parseIsoDate(rawDate);
        if (!on.isValid()) {
            qCWarning(lcDining) << "dining: skipping unparseable date key" << rawDate;
            continue;
        }
        const QJsonValue rawBlocks = obj.value(rawDate);
        if (!rawBlocks.isArray()) {
            qCWarning(lcDining) << "dining:" << rawDate << "did not map to a list of blocks";
            continue;
        }

        DayMenu day{on, {}};
        for (const QJsonValue &rawBlock : rawBlocks.toArray()) {
            if (auto block = parseBlock(rawBlock))
                day.blocks.append(*block);
        }
        days.append(day);
    }

    if (days.isEmpty())
        throw ParseError(QStringLiteral("the dining API returned no usable days"));
    return days;
}

QList<MenuBlock> homeCookingFor(const QList<DayMenu> &days, QDate on)
{
    for (const DayMenu &day : days) {
        if (day.on == on)
            return day.forVenue(HOME_COOKING);
    }
    return {};
}

std::optional<ServingHours> servingHours(QDate on, const QString &slot)
{
    const int weekday = on.dayOfWeek(); // Monday = 1 … Sunday = 7
    const DayHours &hours = weekday == 6 ? SATURDAY : weekday == 7 ? SUNDAY : WEEKDAY;
    const QString key = slot.toCaseFolded();
    if (key == u"breakfast")
        return hours.breakfast;
    if (key == u"lunch")
        return hours.lunch;
    if (key == u"dinner")
        return hours.dinner;
    return std::nullopt;
}

QString formatHours(QTime start, QTime end)
{
    return clockShort(start) + QStringLiteral("–") + clockShort(end);
}

std::optional<std::pair<QDate, MenuBlock>> nextMealBlock(const QList<DayMenu> &days,
                                                         const QDateTime &now, const QString &venue)
{
    const QDate today = now.date();

    QList<DayMenu> ordered = days;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const DayMenu &a, const DayMenu &b) { return a.on < b.on; });

    for (const DayMenu &day : ordered) {
        if (day.on < today)
            continue;
        for (const MenuBlock &block : day.forVenue(venue)) {
            // forVenue() includes all-day stations (slot "anytime"), which are
            // never "next" — they are always on.
            const std::optional<ServingHours> hours = servingHours(day.on, block.slot);
            if (!hours || block.items.isEmpty())
                continue;
            if (day.on > today || now.time() < hours->second)
                return std::make_pair(day.on, block);
        }
    }
    return std::nullopt;
}

} // namespace mycu
