#include "hours.h"

#include "format.h"

#include <algorithm>

namespace mycu {

using namespace hours;

namespace {

constexpr double SPAN = TIMELINE_END - TIMELINE_START;

int minuteOf(const QDateTime &at)
{
    return at.time().hour() * 60 + at.time().minute();
}

QTime timeOf(int minute)
{
    return QTime(0, 0).addSecs((minute % 1440) * 60);
}

QVariantMap segment(int open, int close, const QString &kind)
{
    const int from = std::clamp(open, TIMELINE_START, TIMELINE_END);
    const int to = std::clamp(close, TIMELINE_START, TIMELINE_END);
    return {{QStringLiteral("left"), (from - TIMELINE_START) / SPAN},
            {QStringLiteral("width"), std::max(0, to - from) / SPAN},
            {QStringLiteral("kind"), kind}};
}

// Where a window stands against the clock, when the day shown is today.
QString kindOf(int open, int close, int minute, bool today, const QString &upcoming)
{
    if (!today)
        return upcoming;
    if (close <= minute)
        return QStringLiteral("past");
    if (open <= minute)
        return QStringLiteral("now");
    return upcoming;
}

// "10:30 AM – 7:00 PM".
QString spanText(int open, int close)
{
    return fmt::clock(timeOf(open)) + QStringLiteral(" – ") + fmt::clock(timeOf(close));
}

// Back-to-back windows (hot breakfast, then continental) read as one opening:
// The Commons is open "until 9:30 AM", not "until 8:15 AM".
QDateTime runEnd(const Schedule &schedule, QDateTime closes)
{
    for (bool extended = true; extended;) {
        extended = false;
        for (const Window &window : schedule(closes.date())) {
            if (at(closes.date(), window.open) == closes) {
                closes = at(closes.date(), window.close);
                extended = true;
            }
        }
    }
    return closes;
}

} // namespace

// ---------------------------------------------------------------------------
// TimelineModel
// ---------------------------------------------------------------------------

QHash<int, QByteArray> TimelineModel::roleNames() const
{
    return {{NameRole, "name"}, {StatusRole, "status"}, {LiveRole, "live"}, {SegmentsRole, "segments"}};
}

int TimelineModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant TimelineModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case NameRole:
        return row.name;
    case StatusRole:
        return row.status;
    case LiveRole:
        return row.live;
    case SegmentsRole:
        return row.segments;
    default:
        return {};
    }
}

void TimelineModel::replace(const QList<Row> &rows)
{
    if (rows == m_rows)
        return;
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

// ---------------------------------------------------------------------------
// HoursViewModel
// ---------------------------------------------------------------------------

HoursViewModel::HoursViewModel(QObject *parent)
    : QObject(parent)
    , m_now(QDateTime::currentDateTime())
    , m_timeline(this)
{
    m_dayType = todayType();
    rebuild();
}

QList<HoursViewModel::Place> HoursViewModel::places()
{
    QList<Place> out{{QStringLiteral("The Commons"), QString(), commonsOn}};
    for (const Venue &venue : venues())
        out.append({venue.name, venue.note, [venue](QDate day) { return venue.on(day); }});
    return out;
}

int HoursViewModel::todayType() const
{
    return static_cast<int>(diningDayOf(m_now.date()));
}

QStringList HoursViewModel::dayTypeLabels() const
{
    return {diningDayLabel(DiningDay::Weekday), diningDayLabel(DiningDay::Saturday),
            diningDayLabel(DiningDay::Sunday)};
}

QString HoursViewModel::todayTypeText() const
{
    switch (diningDayOf(m_now.date())) {
    case DiningDay::Saturday:
        return QStringLiteral("Saturday hours");
    case DiningDay::Sunday:
        return QStringLiteral("Sunday hours");
    case DiningDay::Weekday:
        break;
    }
    return QStringLiteral("Weekday hours");
}

QList<HoursViewModel::OpenPlace> HoursViewModel::openDiningPlaces(const QDateTime &now)
{
    QList<OpenPlace> open;
    for (const Place &place : places()) {
        if (const auto opening = currentOpening(place.schedule, now))
            open.append({place.name, place.note, runEnd(place.schedule, opening->closes)});
    }
    std::stable_sort(open.begin(), open.end(),
                     [](const OpenPlace &a, const OpenPlace &b) { return a.closes < b.closes; });
    return open;
}

bool HoursViewModel::anyOpen() const
{
    return !openDiningPlaces(m_now).isEmpty();
}

QString HoursViewModel::openName() const
{
    const QList<OpenPlace> open = openDiningPlaces(m_now);
    if (open.isEmpty())
        return {};
    for (const OpenPlace &place : open) {
        if (place.name == u"The Commons")
            return place.name;
    }
    return open.last().name;
}

QString HoursViewModel::openUntil() const
{
    const QString name = openName();
    for (const OpenPlace &place : openDiningPlaces(m_now)) {
        if (place.name == name)
            return QStringLiteral("until ") + fmt::clock(place.closes.time());
    }
    return {};
}

QStringList HoursViewModel::alsoOpen() const
{
    const QString name = openName();
    QStringList out;
    for (const OpenPlace &place : openDiningPlaces(m_now)) {
        if (place.name != name)
            out.append(place.name);
    }
    return out;
}

namespace {

struct Upcoming
{
    QDateTime opens;
    QStringList names;
};

// The earliest opening still to come today, and everything opening then.
Upcoming upcomingToday(const QDateTime &now, const QList<Venue> &table)
{
    Upcoming next;
    auto consider = [&](const QDateTime &opens, const QString &name) {
        if (opens.date() != now.date() || opens <= now)
            return;
        if (!next.opens.isValid() || opens < next.opens) {
            next.opens = opens;
            next.names = {name};
        } else if (opens == next.opens && !next.names.contains(name)) {
            next.names.append(name);
        }
    };
    for (const Sitting &sitting : commonsSittings(diningDayOf(now.date())))
        consider(at(now.date(), sitting.hours.open), QStringLiteral("Commons ") + sitting.name.toLower());
    for (const Venue &venue : table) {
        // Only venues that are closed now: one already open is not "opening".
        if (isOpen([&venue](QDate d) { return venue.on(d); }, now))
            continue;
        for (const Window &window : venue.on(now.date()))
            consider(at(now.date(), window.open), venue.name);
    }
    return next;
}

} // namespace

QString HoursViewModel::nextOpeningText() const
{
    const Upcoming next = upcomingToday(m_now, venues());
    return next.opens.isValid() ? QStringLiteral("Opening at ") + fmt::clock(next.opens.time()) : QString();
}

QString HoursViewModel::nextOpeningIn() const
{
    const Upcoming next = upcomingToday(m_now, venues());
    return next.opens.isValid() ? QStringLiteral("in ") + fmt::span(m_now.secsTo(next.opens)) : QString();
}

QStringList HoursViewModel::nextOpeningNames() const
{
    return upcomingToday(m_now, venues()).names;
}

double HoursViewModel::nowFraction() const
{
    const int minute = minuteOf(m_now);
    if (!showingToday() || minute < TIMELINE_START)
        return -1.0;
    return (minute - TIMELINE_START) / SPAN;
}

QString HoursViewModel::nowText() const
{
    return fmt::clockBare(m_now.time());
}

QVariantList HoursViewModel::ticks() const
{
    QVariantList out;
    const QList<std::pair<int, QString>> marks = {
        {420, QStringLiteral("7a")},  {600, QStringLiteral("10a")}, {780, QStringLiteral("1p")},
        {960, QStringLiteral("4p")},  {1140, QStringLiteral("7p")}, {1320, QStringLiteral("10p")},
    };
    for (const auto &[minute, label] : marks)
        out.append(QVariantMap{{QStringLiteral("left"), (minute - TIMELINE_START) / SPAN},
                               {QStringLiteral("label"), label}});
    return out;
}

QVariantList HoursViewModel::swipePeriods() const
{
    const int minute = minuteOf(m_now);
    QVariantList out;
    for (const SwipePeriod &period : hours::swipePeriods()) {
        out.append(QVariantMap{{QStringLiteral("name"), period.name},
                               {QStringLiteral("text"), period.text},
                               {QStringLiteral("current"), showingToday() && period.window.contains(minute)}});
    }
    return out;
}

void HoursViewModel::setDayType(int type)
{
    type = std::clamp(type, 0, 2);
    m_followToday = type == todayType();
    if (type == m_dayType)
        return;
    m_dayType = type;
    rebuild();
    emit changed();
}

void HoursViewModel::refreshAll()
{
    // Nothing here shows seconds, so a tick within the same minute says
    // nothing new — and saying it would rebuild the timeline for no reason.
    const QDateTime current = now();
    if (current.date() == m_now.date() && current.time().hour() == m_now.time().hour()
        && current.time().minute() == m_now.time().minute())
        return;
    m_now = current;
    if (m_followToday)
        m_dayType = todayType();
    rebuild();
    emit changed();
}

void HoursViewModel::rebuild()
{
    const bool today = showingToday();
    const int minute = minuteOf(m_now);
    // A date of the type shown: today, or the next one of that kind.
    QDate day = m_now.date();
    for (int i = 0; i < 7 && static_cast<int>(diningDayOf(day)) != m_dayType; ++i)
        day = day.addDays(1);

    QList<TimelineModel::Row> rows;

    // The Commons, sitting by sitting.
    {
        TimelineModel::Row row;
        row.name = QStringLiteral("The Commons");
        const QList<Sitting> sittings = commonsSittings(static_cast<DiningDay>(m_dayType));
        for (const Sitting &s : sittings)
            row.segments.append(segment(s.hours.open, s.hours.close,
                                        kindOf(s.hours.open, s.hours.close, minute, today, QStringLiteral("next"))));
        if (!today) {
            if (!sittings.isEmpty())
                row.status = spanText(sittings.first().hours.open, sittings.last().hours.close);
        } else {
            for (const Sitting &s : sittings) {
                if (s.hours.contains(minute)) {
                    const QDateTime ends = runEnd(commonsOn, at(day, s.hours.close));
                    row.status = s.name + QStringLiteral(" until ") + fmt::clock(ends.time());
                    row.live = true;
                    break;
                }
                if (s.hours.open > minute) {
                    row.status = s.name + QStringLiteral(" at ") + fmt::clock(timeOf(s.hours.open));
                    break;
                }
            }
            if (row.status.isEmpty())
                row.status = QStringLiteral("Closed for the day");
        }
        rows.append(row);
    }

    for (const Venue &venue : venues()) {
        TimelineModel::Row row;
        row.name = venue.name;
        const auto &window = venue.hours[m_dayType];
        const auto &exchange = venue.exchange[m_dayType];
        if (!window) {
            row.status = QStringLiteral("Closed");
            rows.append(row);
            continue;
        }
        const int open = window->open;
        const int close = window->close;
        if (exchange && exchange->close < close) {
            row.segments.append(segment(open, exchange->close,
                                        kindOf(open, exchange->close, minute, today, QStringLiteral("next"))));
            row.segments.append(segment(exchange->close, close,
                                        kindOf(exchange->close, close, minute, today, QStringLiteral("flexOnly"))));
        } else {
            row.segments.append(segment(open, close, kindOf(open, close, minute, today, QStringLiteral("next"))));
        }

        if (!today) {
            row.status = spanText(open, close);
        } else if (window->contains(minute)) {
            row.status = QStringLiteral("Open until ") + fmt::clock(timeOf(close));
            row.live = true;
        } else if (open > minute) {
            row.status = QStringLiteral("Opens ") + fmt::clock(timeOf(open));
        } else {
            row.status = QStringLiteral("Closed at ") + fmt::clock(timeOf(close));
        }
        rows.append(row);
    }

    m_timeline.replace(rows);
}

} // namespace mycu
