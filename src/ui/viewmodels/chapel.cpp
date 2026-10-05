#include "chapel.h"

#include "core/calendar.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/providers/chapel_schedule.h"
#include "format.h"
#include "ui/tasks.h"

#include <QElapsedTimer>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace mycu {

namespace {

// "Absent from Chapel 8/20/2026" -> "Absent from Chapel": the date is on the
// row already.
QString withoutTrailingDate(const QString &text)
{
    static const QRegularExpression date(QStringLiteral("\\s+\\d{1,2}/\\d{1,2}/\\d{2,4}$"));
    QString out = text;
    out.remove(date);
    return out.trimmed();
}

QString sentenceCase(const QString &text)
{
    if (text.isEmpty())
        return text;
    return text.left(1).toUpper() + text.mid(1).toLower();
}

// Whether the failure is the network rather than the server.
bool isOffline(std::exception_ptr error)
{
    try {
        std::rethrow_exception(error);
    } catch (const TransportError &e) {
        return e.httpStatus() == 0;
    } catch (...) {
        return false;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// ChapelListModel
// ---------------------------------------------------------------------------

QHash<int, QByteArray> ChapelListModel::roleNames() const
{
    return {
        {WhenRole, "whenText"},  {ReasonRole, "reason"}, {TypeRole, "entryType"}, {CountRole, "count"},
        {SkipRole, "isSkip"},    {TitleRole, "title"},   {DetailRole, "detail"},
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
    case TitleRole: {
        // What happened, in the ledger's own words.
        const QString reason = withoutTrailingDate(entry.reason);
        return !reason.isEmpty() ? reason : !entry.entryType.isEmpty() ? entry.entryType
                                                                        : QStringLiteral("Ledger entry");
    }
    case DetailRole: {
        // "Thu, Aug 20 · chapel skip". Adjustments have no chapel date; the
        // day they were made is the next best thing.
        QStringList parts;
        const QDateTime when = entry.on.isValid() ? entry.on : entry.createdAt;
        if (when.isValid())
            parts.append(fmt::shortDate(when.date()));
        if (!entry.entryType.isEmpty())
            parts.append(entry.entryType.toLower());
        return parts.join(QStringLiteral(" · "));
    }
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

QStringList seriesParts(const QList<UpcomingChapel> &chapels)
{
    QStringList parts(chapels.size());
    qsizetype start = 0;
    while (start < chapels.size()) {
        qsizetype end = start + 1;
        const UpcomingChapel &first = chapels.at(start);
        while (!first.speakers.isEmpty() && end < chapels.size()) {
            const UpcomingChapel &prev = chapels.at(end - 1);
            const UpcomingChapel &next = chapels.at(end);
            if (next.speakers != first.speakers || !prev.startsAt.isValid() || !next.startsAt.isValid()
                || prev.on().daysTo(next.on()) < 1 || prev.on().daysTo(next.on()) > 3)
                break;
            ++end;
        }
        if (end - start > 1) {
            for (qsizetype i = start; i < end; ++i)
                parts[i] = QStringLiteral("Part %1 of %2").arg(i - start + 1).arg(end - start);
        }
        start = end;
    }
    return parts;
}

QString shortReason(const QString &reason)
{
    static const QRegularExpression credits(QStringLiteral("registered for ([\\d.]+) credits"),
                                            QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = credits.match(reason);
    if (match.hasMatch())
        return match.captured(1) + QStringLiteral(" credits");

    QString text = reason.trimmed();
    if (text.endsWith(QStringLiteral(" Student"), Qt::CaseInsensitive))
        text.chop(8);
    return sentenceCase(text);
}

int ScheduleListModel::weeks() const
{
    return m_rows.isEmpty() ? 0 : m_rows.last().weekIndex + 1;
}

QHash<int, QByteArray> ScheduleListModel::roleNames() const
{
    return {
        {HeaderRole, "isHeader"},       {HeadingRole, "heading"},     {WhoRole, "who"},
        {SubtitleRole, "subtitle"},     {DescriptionRole, "description"}, {DayNameRole, "dayName"},
        {DayNumberRole, "dayNumber"},   {TimeTextRole, "timeText"},   {BadgeRole, "badge"},
        {IsNowRole, "isNow"},           {LivestreamRole, "livestream"}, {IsTodayRole, "isToday"},
        {PartRole, "part"},             {YoutubeIdRole, "youtubeId"}, {StartsAtRole, "startsAt"},
        {DateTextRole, "dateText"},     {WeekIndexRole, "weekIndex"}, {FirstInWeekRole, "firstInWeek"},
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
    case IsTodayRole:
        return row.isToday;
    case PartRole:
        return row.part;
    case YoutubeIdRole:
        return row.youtubeId;
    case StartsAtRole:
        return row.startsAt;
    case DateTextRole:
        return row.dateText;
    case WeekIndexRole:
        return row.weekIndex;
    case FirstInWeekRole:
        return row.firstInWeek;
    default:
        return {};
    }
}

void ScheduleListModel::replace(const QList<UpcomingChapel> &chapels, const QDateTime &now)
{
    const QDate today = now.toLocalTime().date();
    // Over the whole feed, so a series already under way still says "Part 2".
    const QStringList parts = seriesParts(chapels);

    QList<Row> rows;
    QDate lastWeek;
    int weekIndex = -1;
    for (qsizetype i = 0; i < chapels.size(); ++i) {
        const UpcomingChapel &chapel = chapels.at(i);
        if (!chapel.startsAt.isValid() || isOver(chapel, now))
            continue;
        const QDateTime when = chapel.startsAt.toLocalTime();
        const QDate week = mondayOf(when.date());
        if (week != lastWeek) {
            Row header;
            header.isHeader = true;
            header.heading = weekLabel(week, today);
            header.weekIndex = ++weekIndex;
            rows.append(header);
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
        row.dateText = fmt::shortDate(when.date());
        row.isToday = when.date() == today;
        // "Today · in 18 min" while it is still to come today.
        if (happening)
            row.badge = QStringLiteral("Now");
        else if (row.isToday)
            row.badge = QStringLiteral("Today · in ") + fmt::span(now.secsTo(chapel.startsAt));
        else
            row.badge = relativeDay(when.date(), today);
        row.isNow = happening;
        row.livestream = chapel.willLivestream;
        row.part = parts.at(i);
        row.youtubeId = chapel.youtubeId;
        row.startsAt = when;
        row.weekIndex = weekIndex;
        row.firstInWeek = rows.last().isHeader;
        rows.append(row);
    }

    // The same chapels as before: update in place.
    const bool sameRows = rows.size() == m_rows.size()
        && std::equal(rows.cbegin(), rows.cend(), m_rows.cbegin(), [](const Row &a, const Row &b) {
               return a.isHeader == b.isHeader && a.who == b.who && a.startsAt == b.startsAt
                   && a.heading == b.heading;
           });
    if (sameRows) {
        m_rows = rows;
        if (!m_rows.isEmpty())
            emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1));
        return;
    }
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

// ---------------------------------------------------------------------------
// ChapelViewModel
// ---------------------------------------------------------------------------

ChapelViewModel::ChapelViewModel(TransportPtr transport, std::optional<Storage> storage, QObject *parent)
    : QObject(parent)
    , m_transport(std::move(transport))
    , m_storage(std::move(storage))
    , m_model(this)
    , m_schedule(this)
{
    m_skipsStatus.now = [this] { return now(); };
    m_scheduleStatus.now = [this] { return now(); };
    connect(this, &ChapelViewModel::changed, this, &ChapelViewModel::clockChanged);
    hydrate();
}

void ChapelViewModel::resetProvider(const QString &studentId)
{
    m_provider = std::make_shared<ChapelProvider>(m_transport, studentId);
}

void ChapelViewModel::hydrate()
{
    if (!persisting()) {
        resetProvider(QString());
        return;
    }

    m_state = m_storage->session.load();
    resetProvider(m_state.studentId);

    // Parsed again from what was saved, by the same parsers a fetch uses. A
    // copy the parsers no longer accept is simply not shown.
    if (const auto saved = m_storage->cache.load(cachekey::CHAPEL)) {
        try {
            applySummary(buildSummary(saved->payload.toObject()));
            m_skipsStatus.restored(saved->savedAt);
        } catch (const std::exception &e) {
            qCWarning(lcChapel) << "chapel: the saved ledger is unreadable (ignored):" << e.what();
        }
    }
    if (const auto saved = m_storage->cache.load(cachekey::SCHEDULE)) {
        try {
            applySchedule(parseUpcoming(saved->payload));
            m_scheduleStatus.restored(saved->savedAt);
        } catch (const std::exception &e) {
            qCWarning(lcChapel) << "chapel: the saved schedule is unreadable (ignored):" << e.what();
        }
    }
}

QString ChapelViewModel::term() const
{
    const QString label = m_summary.label();
    return !label.isEmpty() ? label : m_state.lastTerm;
}

QString ChapelViewModel::termLabel() const
{
    QString text = term();
    text.replace(QStringLiteral(" Semester"), QString());
    return text;
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

QVariantList ChapelViewModel::allowance() const
{
    // One line is just the total, which the hero already shows.
    if (m_summary.allowance.size() < 2)
        return {};
    QVariantList out;
    bool first = true;
    for (const AllowanceLine &line : m_summary.allowance) {
        QString figure = QString::number(std::abs(line.count));
        if (!first)
            figure.prepend(line.count < 0 ? QStringLiteral("−") : QStringLiteral("+"));
        // "Skips Allowed" says less than its description does.
        const QString label = first && !line.description.isEmpty()
            ? QString(line.description).remove(QStringLiteral(" semester"))
            : sentenceCase(line.reason);
        out.append(QVariantMap{{QStringLiteral("figure"), figure}, {QStringLiteral("label"), label}});
        first = false;
    }
    return out;
}

QStringList ChapelViewModel::requirementReasons() const
{
    QStringList out;
    for (const QString &reason : m_summary.requirementReasons)
        out.append(shortReason(reason));
    return out;
}

double ChapelViewModel::remainingFraction() const
{
    const std::optional<int> total = m_summary.total;
    const std::optional<int> left = m_summary.remaining;
    if (!total || *total <= 0 || !left)
        return 0.0;
    return std::clamp(static_cast<double>(*left) / *total, 0.0, 1.0);
}

QString ChapelViewModel::skipsPerWeekText() const
{
    const QDate today = now().date();
    const std::optional<TermWindow> current = currentTerm(today);
    if (!current || !m_summary.remaining)
        return {};

    const QString through = QStringLiteral(" through ") + fmt::monthDay(current->endIn(today.year()));
    const int left = *m_summary.remaining;
    if (left <= 0)
        return QStringLiteral("None to spare") + through;

    const double weeks = current->daysLeft(today) / 7.0;
    if (weeks < 1.0)
        return QStringLiteral("Through ") + fmt::monthDay(current->endIn(today.year()));
    const double perWeek = left / weeks;
    if (perWeek >= 0.95)
        return QStringLiteral("About %1 a week").arg(std::lround(perWeek)) + through;
    return QStringLiteral("About 1 every %1 weeks").arg(std::lround(1.0 / perWeek)) + through;
}

QString ChapelViewModel::historyText() const
{
    const qsizetype n = m_summary.entries.size();
    return n == 1 ? QStringLiteral("1 entry") : QStringLiteral("%1 entries").arg(n);
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
    const QDate today = now().date();
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
    const QDate today = now().date();
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

QString ChapelViewModel::nextChapelTime() const
{
    if (!m_next || !m_next->startsAt.isValid())
        return {};
    return fmt::clock(m_next->startsAt.toLocalTime().time());
}

QDateTime ChapelViewModel::nextChapelStartsAt() const
{
    return m_next ? m_next->startsAt.toLocalTime() : QDateTime();
}

QString ChapelViewModel::nextChapelDescription() const
{
    return m_next ? m_next->description : QString();
}

QString ChapelViewModel::nextChapelCountdown() const
{
    if (!m_next || !m_next->startsAt.isValid())
        return {};
    const QDateTime current = now();
    if (isHappening(*m_next, current))
        return QStringLiteral("now");
    const QDate day = m_next->startsAt.toLocalTime().date();
    if (day == current.date())
        return QStringLiteral("in ") + fmt::span(current.secsTo(m_next->startsAt));
    if (day == current.date().addDays(1))
        return QStringLiteral("tomorrow");
    return fmt::dayLong(day);
}

bool ChapelViewModel::chapelToday() const
{
    return m_next && m_next->startsAt.isValid() && m_next->startsAt.toLocalTime().date() == now().date();
}

QString ChapelViewModel::fromNowText(const QDateTime &when) const
{
    if (!when.isValid())
        return {};
    const QDateTime current = now();
    if (when <= current)
        return when.addSecs(CHAPEL_LENGTH_SECS) > current ? QStringLiteral("Now") : QString();
    return fmt::span(current.secsTo(when));
}

QString ChapelViewModel::watchUrl(const QString &youtubeId) const
{
    if (youtubeId.isEmpty())
        return {};
    return QStringLiteral("https://www.youtube.com/watch?v=") + youtubeId;
}

QString ChapelViewModel::scheduleEmptyText() const
{
    if (m_schedule.rowCount())
        return {};
    if (!m_scheduleError.isEmpty())
        return m_scheduleError;
    if (!m_scheduleStatus.hasData())
        return QStringLiteral("Loading the chapel schedule…");
    // Over breaks and the summer the feed is genuinely empty.
    return QStringLiteral("No chapels scheduled right now.");
}

void ChapelViewModel::applySchedule(const QList<UpcomingChapel> &chapels)
{
    m_chapels = chapels;
    m_next = nextChapel(chapels, now());
    m_schedule.replace(chapels, now());
}

void ChapelViewModel::refreshSchedule()
{
    if (m_scheduleBusy)
        return;
    m_scheduleBusy = true;
    m_scheduleStatus.begin();
    emit changed();

    const int generation = m_generation;
    // Page 1 carries the next chapel, so it goes on screen at once; the rest of
    // the term follows.
    runInBackgroundWithPartial<QJsonObject>(
        this,
        [transport = m_transport](const std::function<void(const QJsonObject &)> &report) {
            QElapsedTimer timer;
            timer.start();
            QJsonObject payload = fetchSchedulePayload(transport, report);
            qCInfo(lcChapel) << "chapel schedule: fetched in" << timer.elapsed() << "ms";
            return payload;
        },
        [this, generation](const QJsonObject &firstPage) {
            if (generation != m_generation || !m_scheduleBusy)
                return;
            try {
                applySchedule(parseUpcoming(firstPage));
                emit changed();
            } catch (const std::exception &) {
                // The whole fetch fails the same way, and reports it.
            }
        },
        [this, generation](const QJsonObject &payload) {
            if (generation == m_generation)
                onSchedulePayloadLoaded(payload);
        },
        [this, generation](std::exception_ptr error) {
            if (generation == m_generation)
                onScheduleFailed(error);
        });
}

void ChapelViewModel::onSchedulePayloadLoaded(const QJsonObject &payload)
{
    QList<UpcomingChapel> chapels;
    try {
        chapels = parseUpcoming(payload);
    } catch (...) {
        onScheduleFailed(std::current_exception());
        return;
    }
    if (persisting())
        m_storage->cache.save(cachekey::SCHEDULE, payload, now());
    onScheduleLoaded(chapels);
}

void ChapelViewModel::onScheduleLoaded(const QList<UpcomingChapel> &chapels)
{
    m_scheduleBusy = false;
    applySchedule(chapels);
    m_scheduleError.clear();
    m_scheduleStatus.succeeded(now());
    qCInfo(lcChapel).noquote() << "chapel schedule:" << chapels.size() << "chapels, next is"
                               << (m_next ? m_next->who() : QStringLiteral("(none)"));
    emit changed();
}

void ChapelViewModel::onScheduleFailed(std::exception_ptr error)
{
    m_scheduleBusy = false;
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
    m_scheduleStatus.failed(m_scheduleError, isOffline(error));
    qCWarning(lcChapel).noquote() << "chapel schedule unavailable:" << describe(error);
    emit changed();
}

void ChapelViewModel::refreshAll()
{
    refresh();
    refreshSchedule();
}

void ChapelViewModel::tick()
{
    const std::optional<UpcomingChapel> next = nextChapel(m_chapels, now());
    const bool moved = next != m_next;
    m_next = next;
    // In place unless a chapel ended: see ScheduleListModel::replace().
    m_schedule.replace(m_chapels, now());
    m_skipsStatus.tick();
    m_scheduleStatus.tick();
    if (moved)
        emit changed();
    else
        emit clockChanged();
}

void ChapelViewModel::refresh()
{
    if (m_busy)
        return;

    m_busy = true;
    m_error.clear();
    m_skipsStatus.begin();
    emit changed();

    const int generation = m_generation;
    runInBackground(
        this,
        [provider = m_provider] {
            QElapsedTimer timer;
            timer.start();
            QJsonObject payload = provider->fetchPayload();
            qCInfo(lcChapel) << "chapel: fetched in" << timer.elapsed() << "ms";
            return payload;
        },
        [this, generation](const QJsonObject &payload) {
            if (generation == m_generation)
                onPayloadLoaded(payload);
        },
        [this, generation](std::exception_ptr error) {
            if (generation == m_generation)
                onFailed(error);
        });
}

void ChapelViewModel::onPayloadLoaded(const QJsonObject &payload)
{
    ChapelSummary summary;
    try {
        summary = buildSummary(payload);
    } catch (...) {
        onFailed(std::current_exception());
        return;
    }
    if (persisting())
        m_storage->cache.save(cachekey::CHAPEL, payload, now());
    onLoaded(summary);
}

void ChapelViewModel::applySummary(const ChapelSummary &summary)
{
    m_summary = summary;
    m_model.replace(summary.entries);
}

void ChapelViewModel::onLoaded(const ChapelSummary &summary)
{
    applySummary(summary);
    m_busy = false;
    m_error.clear();
    m_skipsStatus.succeeded(now());

    if (persisting()) {
        const QString label = summary.label();
        const QString id = m_provider->knownStudentId();
        m_state = m_storage->session.update([&](SessionState &state) {
            if (!label.isEmpty())
                state.lastTerm = label;
            if (!id.isEmpty())
                state.studentId = id;
            state.lastSuccess = QDateTime::currentMSecsSinceEpoch() / 1000.0;
        });
    }

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
        // lifecycle. Hand it to the login flow and stay quiet.
        m_error.clear();
        m_skipsStatus.stopped();
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

    m_skipsStatus.failed(m_error, isOffline(error));
    qCCritical(lcChapel).noquote() << "chapel refresh failed:" << describe(error);
    emit changed();
}

void ChapelViewModel::clearPersonal()
{
    ++m_generation;
    m_busy = false;
    m_error.clear();
    m_summary = ChapelSummary();
    m_model.replace({});
    m_state = SessionState();
    m_skipsStatus.reset();
    resetProvider(QString());
    emit changed();
}

void ChapelViewModel::setPreview(bool on)
{
    if (on == m_preview)
        return;
    m_preview = on;
    ++m_generation;
    m_busy = false;
    m_scheduleBusy = false;
    m_error.clear();
    m_scheduleError.clear();
    m_summary = ChapelSummary();
    m_model.replace({});
    m_state = SessionState();
    applySchedule({});
    m_skipsStatus.reset();
    m_scheduleStatus.reset();
    hydrate();
    emit changed();
}

} // namespace mycu
