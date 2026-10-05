#include "campus.h"

#include "core/hours.h"
#include "format.h"
#include "search.h"
#include "ui/settings.h"

#include <QMap>

#include <algorithm>

namespace mycu {

using namespace hours;

namespace {

// "Opens 7:00 AM" today, "Opens Mon 7:00 AM" another day.
QString opensText(const QDateTime &opens, const QDateTime &now)
{
    if (!opens.isValid())
        return QStringLiteral("Closed");
    if (opens.date() == now.date())
        return QStringLiteral("Opens ") + fmt::clock(opens.time());
    if (opens.date() == now.date().addDays(1) && opens.time() < QTime(12, 0))
        return QStringLiteral("Opens ") + fmt::clock(opens.time());
    return QStringLiteral("Opens ") + fmt::dayShort(opens.date()) + u' ' + fmt::clock(opens.time());
}

} // namespace

CampusViewModel::CampusViewModel(QObject *parent)
    : QObject(parent)
    , m_now(QDateTime::currentDateTime())
{}

void CampusViewModel::attachSettings(SettingsController *settings)
{
    m_settings = settings;
    if (settings) {
        m_favorites = settings->favorites();
        emit changed();
    }
}

QList<CampusViewModel::Entry> CampusViewModel::entries() const
{
    QList<Entry> out;
    for (const Building &building : buildings()) {
        const Schedule schedule = [building](QDate day) { return building.on(day); };
        Entry entry{building.name, building.code, building.note};
        if (const auto opening = currentOpening(schedule, m_now)) {
            entry.isOpen = true;
            entry.closes = opening->closes;
            entry.closingSoon = m_now.secsTo(opening->closes) <= CLOSING_SOON_SECS;
        } else if (const auto next = nextOpening(schedule, m_now)) {
            entry.opens = next->opens;
        }
        out.append(entry);
    }
    return out;
}

bool CampusViewModel::matches(const Entry &entry) const
{
    switch (m_filter) {
    case 1:
        if (!entry.isOpen)
            return false;
        break;
    case 2:
        if (!entry.closingSoon)
            return false;
        break;
    case 3:
        if (entry.isOpen)
            return false;
        break;
    default:
        break;
    }
    if (m_query.trimmed().isEmpty())
        return true;
    return search::matches(search::tokens(m_query), entry.name + u' ' + entry.code);
}

QVariantMap CampusViewModel::describe(const Entry &entry) const
{
    return {
        {QStringLiteral("name"), entry.name},
        {QStringLiteral("code"), entry.code},
        {QStringLiteral("note"), entry.note},
        {QStringLiteral("isOpen"), entry.isOpen},
        {QStringLiteral("closesText"), entry.isOpen ? QStringLiteral("Closes ") + fmt::clock(entry.closes.time())
                                                    : opensText(entry.opens, m_now)},
        {QStringLiteral("closesIn"), entry.isOpen ? fmt::span(m_now.secsTo(entry.closes)) : QString()},
        {QStringLiteral("closingSoon"), entry.closingSoon},
        {QStringLiteral("favorite"), m_favorites.contains(entry.name)},
    };
}

QVariantList CampusViewModel::groups() const
{
    // Open buildings by the minute they close; the closed ones apart.
    QMap<QDateTime, QVariantList> open;
    QVariantList closed;
    QDateTime firstOpening;
    for (const Entry &entry : entries()) {
        if (!matches(entry))
            continue;
        if (entry.isOpen) {
            open[entry.closes].append(describe(entry));
        } else {
            closed.append(describe(entry));
            if (entry.opens.isValid() && (!firstOpening.isValid() || entry.opens < firstOpening))
                firstOpening = entry.opens;
        }
    }

    QVariantList out;
    for (auto it = open.cbegin(); it != open.cend(); ++it) {
        const qint64 left = m_now.secsTo(it.key());
        out.append(QVariantMap{
            {QStringLiteral("title"), QStringLiteral("Closes at ") + fmt::clock(it.key().time())},
            {QStringLiteral("badge"), QStringLiteral("in ") + fmt::span(left)},
            {QStringLiteral("soon"), left <= CLOSING_SOON_SECS},
            {QStringLiteral("closed"), false},
            {QStringLiteral("buildings"), it.value()},
        });
    }
    if (!closed.isEmpty()) {
        out.append(QVariantMap{
            {QStringLiteral("title"), QStringLiteral("Closed now")},
            {QStringLiteral("badge"), opensText(firstOpening, m_now)},
            {QStringLiteral("soon"), false},
            {QStringLiteral("closed"), true},
            {QStringLiteral("buildings"), closed},
        });
    }
    return out;
}

QVariantList CampusViewModel::favoriteBuildings() const
{
    const QList<Entry> all = entries();
    QVariantList out;
    for (const QString &name : m_favorites) {
        for (const Entry &entry : all) {
            if (entry.name == name)
                out.append(describe(entry));
        }
    }
    return out;
}

int CampusViewModel::openCount() const
{
    const QList<Entry> all = entries();
    return static_cast<int>(std::count_if(all.cbegin(), all.cend(), [](const Entry &e) { return e.isOpen; }));
}

int CampusViewModel::closingSoonCount() const
{
    const QList<Entry> all = entries();
    return static_cast<int>(std::count_if(all.cbegin(), all.cend(), [](const Entry &e) { return e.closingSoon; }));
}

int CampusViewModel::closedCount() const
{
    const QList<Entry> all = entries();
    return static_cast<int>(std::count_if(all.cbegin(), all.cend(), [](const Entry &e) { return !e.isOpen; }));
}

QString CampusViewModel::searchPlaceholder() const
{
    return QStringLiteral("Search %1 buildings").arg(buildings().size());
}

void CampusViewModel::setFilter(int filter)
{
    filter = std::clamp(filter, 0, 3);
    m_filter = filter == m_filter ? 0 : filter;
    emit changed();
}

void CampusViewModel::setQuery(const QString &query)
{
    if (query == m_query)
        return;
    m_query = query;
    emit changed();
}

void CampusViewModel::toggleFavorite(const QString &name)
{
    if (m_settings) {
        m_settings->toggleFavorite(name);
        m_favorites = m_settings->favorites();
    } else if (!m_favorites.removeOne(name)) {
        m_favorites.append(name);
    }
    emit changed();
}

bool CampusViewModel::isFavorite(const QString &name) const
{
    return m_favorites.contains(name);
}

void CampusViewModel::refreshAll()
{
    // Once a minute is all the countdowns need; more would rebuild every
    // building's row for nothing.
    const QDateTime current = now();
    if (current.date() == m_now.date() && current.time().hour() == m_now.time().hour()
        && current.time().minute() == m_now.time().minute())
        return;
    m_now = current;
    emit changed();
}

} // namespace mycu
