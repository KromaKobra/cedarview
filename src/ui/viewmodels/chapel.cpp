#include "chapel.h"

#include "core/errors.h"
#include "core/log.h"
#include "core/providers/chapel.h"
#include "core/providers/chapel_schedule.h"
#include "format.h"
#include "ui/tasks.h"

#include <algorithm>

namespace mycu {

// ---------------------------------------------------------------------------
// ChapelListModel
// ---------------------------------------------------------------------------

QHash<int, QByteArray> ChapelListModel::roleNames() const
{
    return {
        {WhenRole, "whenText"},
        {ReasonRole, "reason"},
        {TypeRole, "entryType"},
        {CountRole, "count"},
        {SkipRole, "isSkip"},
    };
}

int ChapelListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant ChapelListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const ChapelLedgerEntry &entry = m_entries.at(index.row());
    switch (role) {
    case WhenRole:
        return entry.when();
    case ReasonRole:
        // Suppress the reason when `when` is already showing it, which happens
        // for every undated adjustment.
        return entry.when() == entry.reason ? QString() : entry.reason;
    case TypeRole:
        return entry.entryType;
    case CountRole:
        // Signed, and rendered as "+1"/"-1" by the delegate: a manual
        // adjustment giving a skip back should not look like another skip.
        return entry.count;
    case SkipRole:
        return entry.isSkip();
    default:
        return {};
    }
}

void ChapelListModel::replace(const QList<ChapelLedgerEntry> &entries)
{
    beginResetModel();
    m_entries = entries;
    endResetModel();
}

// ---------------------------------------------------------------------------
// ScheduleListModel
// ---------------------------------------------------------------------------

namespace {

QDate mondayOf(QDate day)
{
    return day.addDays(-(day.dayOfWeek() - 1));
}

// "This week" / "Next week" / "Week of Oct 5".
QString weekLabel(QDate monday, QDate today)
{
    const QDate thisMonday = mondayOf(today);
    if (monday == thisMonday)
        return QStringLiteral("This week");
    if (monday == thisMonday.addDays(7))
        return QStringLiteral("Next week");
    return QStringLiteral("Week of ") + fmt::monthDay(monday);
}

QString relativeDay(QDate day, QDate today)
{
    if (day == today)
        return QStringLiteral("Today");
    if (day == today.addDays(1))
        return QStringLiteral("Tomorrow");
    return {};
}

} // namespace

QHash<int, QByteArray> ScheduleListModel::roleNames() const
{
    return {
        {HeaderRole, "isHeader"},
        {HeadingRole, "heading"},
        {WhoRole, "who"},
        {SubtitleRole, "subtitle"},
        {DescriptionRole, "description"},
        {DayNameRole, "dayName"},
        {DayNumberRole, "dayNumber"},
        {TimeTextRole, "timeText"},
        {BadgeRole, "badge"},
        {IsNowRole, "isNow"},
        {LivestreamRole, "livestream"},
    };
}

int ScheduleListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant ScheduleListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Row &row = m_rows.at(index.row());
    switch (role) {
    case HeaderRole:
        return row.isHeader;
    case HeadingRole:
        return row.heading;
    case WhoRole:
        return row.who;
    case SubtitleRole:
        return row.subtitle;
    case DescriptionRole:
        return row.description;
    case DayNameRole:
        return row.dayName;
    case DayNumberRole:
        return row.dayNumber;
    case TimeTextRole:
        return row.timeText;
    case BadgeRole:
        return row.badge;
    case IsNowRole:
        return row.isNow;
    case LivestreamRole:
        return row.livestream;
    default:
        return {};
    }
}

void ScheduleListModel::replace(const QList<UpcomingChapel> &chapels, const QDateTime &now)
{
    const QDate today = now.toLocalTime().date();
    beginResetModel();
    m_rows.clear();
    QDate lastWeek;
    for (const UpcomingChapel &chapel : chapels) {
        if (!chapel.startsAt.isValid() || isOver(chapel, now))
            continue;
        const QDateTime when = chapel.startsAt.toLocalTime();
        const QDate week = mondayOf(when.date());
        if (week != lastWeek) {
            Row header;
            header.isHeader = true;
            header.heading = weekLabel(week, today);
            m_rows.append(header);
            lastWeek = week;
        }
        const bool happening = isHappening(chapel, now);
        Row row;
        row.who = chapel.who();
        row.subtitle = chapel.isSameAsTitle() ? QString() : chapel.title;
        row.description = chapel.description;
        row.dayName = fmt::dayShort(when.date()).toUpper();
        row.dayNumber = QString::number(when.date().day());
        row.timeText = fmt::clock(when.time());
        row.badge = happening ? QStringLiteral("Now") : relativeDay(when.date(), today);
        row.isNow = happening;
        row.livestream = chapel.willLivestream;
        m_rows.append(row);
    }
    endResetModel();
}

// ---------------------------------------------------------------------------
// ChapelViewModel
// ---------------------------------------------------------------------------

ChapelViewModel::ChapelViewModel(TransportPtr transport, SessionStore store, QObject *parent)
    : QObject(parent)
    , m_transport(std::move(transport))
    , m_store(std::move(store))
    , m_state(m_store.load())
    , m_model(this)
    , m_schedule(this)
{}

QString ChapelViewModel::term() const
{
    const QString label = m_summary.label();
    return !label.isEmpty() ? label : m_state.lastTerm;
}

QString ChapelViewModel::allowanceText() const
{
    if (m_summary.allowance.size() < 2)
        return {};
    QStringList parts;
    for (const AllowanceLine &line : m_summary.allowance)
        parts.append(QString::number(line.count) + u' ' + line.reason.toLower());
    return parts.join(QStringLiteral(" + "));
}

double ChapelViewModel::remainingFraction() const
{
    const std::optional<int> total = m_summary.total;
    const std::optional<int> left = m_summary.remaining;
    if (!total || *total <= 0 || !left)
        return 0.0;
    return std::clamp(static_cast<double>(*left) / *total, 0.0, 1.0);
}

QString ChapelViewModel::nextSpeaker() const
{
    return m_next ? m_next->who() : QString();
}

QString ChapelViewModel::nextChapelWhen() const
{
    if (!m_next || !m_next->startsAt.isValid())
        return {};

    const QDateTime when = m_next->startsAt.toLocalTime();
    const QDate today = QDate::currentDate();
    QString day;
    if (when.date() == today)
        day = QStringLiteral("Today");
    else if (when.date() == today.addDays(1))
        day = QStringLiteral("Tomorrow");
    else
        day = fmt::dayShort(when.date());
    return day + u' ' + fmt::clock(when.time());
}

QString ChapelViewModel::nextChapelDay() const
{
    if (!m_next || !m_next->startsAt.isValid())
        return {};

    const QDate when = m_next->startsAt.toLocalTime().date();
    const QDate today = QDate::currentDate();
    if (when == today)
        return QStringLiteral("Today");
    if (when == today.addDays(1))
        return QStringLiteral("Tomorrow");
    return fmt::dayLong(when);
}

QString ChapelViewModel::nextChapelDateText() const
{
    if (!m_next || !m_next->startsAt.isValid())
        return {};

    const QDateTime when = m_next->startsAt.toLocalTime();
    return fmt::shortDate(when.date()) + QStringLiteral(" · ") + fmt::clock(when.time());
}

QString ChapelViewModel::nextChapelTitle() const
{
    if (!m_next || m_next->isSameAsTitle())
        return {};
    return m_next->title;
}

QString ChapelViewModel::scheduleEmptyText() const
{
    if (m_schedule.rowCount())
        return {};
    if (!m_scheduleError.isEmpty())
        return m_scheduleError;
    if (!m_scheduleLoaded)
        return QStringLiteral("Loading the chapel schedule…");
    // Over breaks and the summer the feed is genuinely empty.
    return QStringLiteral("No chapels scheduled right now.");
}

void ChapelViewModel::refreshSchedule()
{
    runInBackground(
        this, [transport = m_transport] { return fetchSchedule(transport); },
        [this](const QList<UpcomingChapel> &chapels) { onScheduleLoaded(chapels); },
        [this](std::exception_ptr error) { onScheduleFailed(error); });
}

void ChapelViewModel::onScheduleLoaded(const QList<UpcomingChapel> &chapels)
{
    m_next = nextChapel(chapels);
    m_schedule.replace(chapels, now());
    m_scheduleLoaded = true;
    m_scheduleError.clear();
    qCInfo(lcChapel).noquote() << "chapel schedule:" << chapels.size() << "chapels, next is"
                               << (m_next ? m_next->who() : QStringLiteral("(none)"));
    emit changed();
}

void ChapelViewModel::onScheduleFailed(std::exception_ptr error)
{
    // No banner: the summary screen's attendance figures matter more than its
    // speaker line. The Chapel tab, which is *about* the schedule, says what
    // went wrong in its own empty text instead.
    try {
        std::rethrow_exception(error);
    } catch (const ParseError &) {
        m_scheduleError = QStringLiteral("The chapel schedule feed changed shape.");
    } catch (const TransportError &e) {
        m_scheduleError = QStringLiteral("Couldn't reach the chapel schedule: ") + e.message();
    } catch (...) {
        m_scheduleError = QStringLiteral("Unexpected error: ") + describe(error);
    }
    qCWarning(lcChapel).noquote() << "chapel schedule unavailable:" << describe(error);
    emit changed();
}

void ChapelViewModel::refreshAll()
{
    refresh();
    refreshSchedule();
}

void ChapelViewModel::refresh()
{
    if (m_busy)
        return;

    m_busy = true;
    m_error.clear();
    emit changed();

    runInBackground(
        this, [provider = ChapelProvider(m_transport)]() mutable { return provider.fetch(); },
        [this](const ChapelSummary &summary) { onLoaded(summary); },
        [this](std::exception_ptr error) { onFailed(error); });
}

void ChapelViewModel::onLoaded(const ChapelSummary &summary)
{
    m_summary = summary;
    m_model.replace(summary.entries);
    m_busy = false;
    m_loaded = true;
    m_error.clear();

    if (!summary.label().isEmpty())
        m_state.lastTerm = summary.label();
    m_store.markSuccess(m_state);

    auto figure = [](std::optional<int> n) { return n ? QString::number(*n) : QStringLiteral("None"); };
    qCInfo(lcChapel).noquote() << QStringLiteral("chapel: %1 ledger entries, used=%2 of %3, remaining=%4")
                                      .arg(summary.entries.size())
                                      .arg(figure(summary.used), figure(summary.total),
                                           figure(summary.remaining));
    emit changed();
}

void ChapelViewModel::onFailed(std::exception_ptr error)
{
    m_busy = false;

    try {
        std::rethrow_exception(error);
    } catch (const SessionExpired &) {
        // Not an error the user should read — it is a normal part of the
        // lifecycle. Hand it to the login controller and stay quiet.
        m_error.clear();
        emit changed();
        emit sessionExpired();
        return;
    } catch (const ParseError &) {
        m_error = QStringLiteral("Cedarville's chapel page didn't look the way this app expects. "
                                 "It has probably changed — run scripts/check-live to confirm.");
    } catch (const TransportError &e) {
        m_error = QStringLiteral("Couldn't reach Self-Service: ") + e.message();
    } catch (...) {
        m_error = QStringLiteral("Unexpected error: ") + describe(error);
    }

    qCCritical(lcChapel).noquote() << "chapel refresh failed:" << describe(error);
    emit changed();
}

} // namespace mycu
