#include "chapel_schedule.h"

#include "../log.h"
#include "../pyjson.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTimeZone>

#include <algorithm>

namespace mycu {

namespace {

// Parse `2026-09-17T14:00:00Z` into a local-time datetime.
//
// Invalid rather than throwing: one unreadable date should cost that one entry,
// not the whole schedule.
QDateTime parseUtc(const QJsonValue &value)
{
    if (!json::truthy(value))
        return {};
    QDateTime parsed = json::parseIsoDateTime(json::str(value));
    if (!parsed.isValid()) {
        qCDebug(lcSchedule) << "chapel schedule: unparseable date" << json::repr(value);
        return {};
    }
    if (parsed.timeSpec() == Qt::LocalTime) {
        // The API has always sent an explicit Z. If that ever changes, assume
        // UTC rather than local — guessing local would shift every time by the
        // viewer's offset.
        parsed = QDateTime(parsed.date(), parsed.time(), QTimeZone::UTC);
    }
    return parsed.toLocalTime();
}

std::optional<UpcomingChapel> parseItem(const QJsonValue &raw)
{
    if (!raw.isObject())
        return std::nullopt;
    const QJsonObject obj = raw.toObject();

    UpcomingChapel chapel;
    for (const QJsonValue &speaker : obj.value(QStringLiteral("Speakers")).toArray()) {
        if (!speaker.isString())
            continue;
        const QString name = speaker.toString().trimmed();
        if (!name.isEmpty())
            chapel.speakers.append(name);
    }
    chapel.startsAt = parseUtc(obj.value(QStringLiteral("Date")));
    chapel.title = obj.value(QStringLiteral("Title")).toString().trimmed();
    chapel.description = obj.value(QStringLiteral("Description")).toString().trimmed();
    chapel.willLivestream = json::truthy(obj.value(QStringLiteral("WillLiveStream")));
    return chapel;
}

// Sort by time, putting undated entries last rather than failing the
// comparison. Upstream order has been chronological so far, but that is not
// promised anywhere.
QList<UpcomingChapel> soonestFirst(QList<UpcomingChapel> chapels)
{
    std::stable_sort(chapels.begin(), chapels.end(),
                     [](const UpcomingChapel &a, const UpcomingChapel &b) {
                         const bool aNone = !a.startsAt.isValid();
                         const bool bNone = !b.startsAt.isValid();
                         if (aNone != bNone)
                             return bNone;
                         if (aNone)
                             return false;
                         return a.startsAt < b.startsAt;
                     });
    return chapels;
}

// "['Items', 'TotalCount']" — the keys a reshaped response did have.
QString keyList(const QJsonObject &obj)
{
    QStringList keys = obj.keys();
    keys.sort();
    for (QString &key : keys)
        key = u'\'' + key + u'\'';
    return u'[' + keys.join(QStringLiteral(", ")) + u']';
}

} // namespace

ChapelScheduleProvider::ChapelScheduleProvider(TransportPtr transport, int count, int page)
    : m_transport(std::move(transport))
    , m_count(std::max(1, count))
    , m_page(std::max(1, page))
{}

QString ChapelScheduleProvider::path() const
{
    return QStringLiteral("%1?page=%2&count=%3").arg(UPCOMING_PATH).arg(m_page).arg(m_count);
}

QList<UpcomingChapel> ChapelScheduleProvider::fetch()
{
    const Response response = m_transport->get(path());
    return parse(response.raiseForSession());
}

QList<UpcomingChapel> ChapelScheduleProvider::parse(const Response &response)
{
    return parseUpcoming(response.json());
}

QList<UpcomingChapel> parseUpcoming(const QJsonValue &payload)
{
    if (!payload.isObject()) {
        throw ParseError(QStringLiteral("expected an object from the chapel schedule API, got %1")
                             .arg(json::typeName(payload)));
    }
    const QJsonObject obj = payload.toObject();

    const QJsonValue items = obj.value(QStringLiteral("Items"));
    if (items.isUndefined() || items.isNull()) {
        throw ParseError(QStringLiteral("no 'Items' key in the response; keys were %1").arg(keyList(obj)));
    }
    if (!items.isArray()) {
        throw ParseError(QStringLiteral("'Items' was %1, expected a list").arg(json::typeName(items)));
    }

    QList<UpcomingChapel> chapels;
    for (const QJsonValue &item : items.toArray()) {
        if (auto chapel = parseItem(item))
            chapels.append(*chapel);
    }
    return soonestFirst(std::move(chapels));
}

QList<UpcomingChapel> fetchSchedule(const TransportPtr &transport, int pageSize, int maxPages)
{
    QList<UpcomingChapel> chapels;
    bool ranOut = false;
    for (int page = 1; page <= maxPages; ++page) {
        const QList<UpcomingChapel> batch =
            ChapelScheduleProvider(transport, pageSize, page).fetch();
        for (const UpcomingChapel &chapel : batch) {
            if (!chapels.contains(chapel))
                chapels.append(chapel);
        }
        if (batch.size() < pageSize) {
            ranOut = true;
            break;
        }
    }
    if (!ranOut)
        qCWarning(lcSchedule) << "chapel schedule: stopped after" << maxPages << "pages";
    return soonestFirst(std::move(chapels));
}

std::optional<UpcomingChapel> nextChapel(const QList<UpcomingChapel> &chapels, const QDateTime &now)
{
    for (const UpcomingChapel &chapel : chapels) {
        if (chapel.startsAt.isValid() && chapel.startsAt > now)
            return chapel;
    }
    return std::nullopt;
}

bool isOver(const UpcomingChapel &chapel, const QDateTime &now)
{
    return chapel.startsAt.isValid() && chapel.startsAt.addSecs(CHAPEL_LENGTH_SECS) <= now;
}

bool isHappening(const UpcomingChapel &chapel, const QDateTime &now)
{
    return chapel.startsAt.isValid() && chapel.startsAt <= now && !isOver(chapel, now);
}

} // namespace mycu
