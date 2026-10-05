#include "today.h"

#include "chapel.h"
#include "core/curfew.h"
#include "core/hours.h"
#include "core/providers/chapel_schedule.h"
#include "curfew.h"
#include "dining.h"
#include "format.h"
#include "hours.h"

#include <algorithm>

namespace mycu {

using namespace hours;

namespace {

// How far ahead the next thing still gets an "in 48 min" chip.
constexpr qint64 CHIP_WITHIN_SECS = 3 * 60 * 60;

QString meridiem(QTime at)
{
    return at.hour() < 12 ? QStringLiteral("AM") : QStringLiteral("PM");
}

QTime timeOfDay(int minute)
{
    return QTime(0, 0).addSecs((minute % 1440) * 60);
}

QList<MenuItem> itemsAt(const QList<DayMenu> &menus, QDate day, const QString &slot, const QString &venue)
{
    for (const DayMenu &menu : menus) {
        if (menu.on != day)
            continue;
        QList<MenuItem> items;
        for (const MenuBlock &block : menu.forVenue(venue)) {
            if (block.slot.toCaseFolded() == slot)
                items.append(block.items);
        }
        return items;
    }
    return {};
}

QStringList names(const QList<MenuItem> &items, int limit)
{
    QStringList out;
    for (const MenuItem &item : items) {
        if (out.size() >= limit)
            break;
        out.append(item.name);
    }
    return out;
}

// "Garden Bites: Broccoli Alfredo" — the vegetarian dish, which is the first
// thing Garden Bites lists.
QString gardenLine(const QList<DayMenu> &menus, QDate day, const QString &slot)
{
    const QList<MenuItem> items = itemsAt(menus, day, slot, GARDEN_BITES);
    return items.isEmpty() ? QString() : GARDEN_BITES + QStringLiteral(": ") + items.first().name;
}

std::optional<UpcomingChapel> chapelOn(const QList<UpcomingChapel> &chapels, QDate day, const QDateTime &now)
{
    for (const UpcomingChapel &chapel : chapels) {
        if (chapel.startsAt.isValid() && chapel.startsAt.toLocalTime().date() == day && !isOver(chapel, now))
            return chapel;
    }
    return std::nullopt;
}

} // namespace

// ---------------------------------------------------------------------------
// AgendaModel
// ---------------------------------------------------------------------------

QHash<int, QByteArray> AgendaModel::roleNames() const
{
    return {
        {TimeRole, "time"},     {MeridiemRole, "meridiem"}, {TitleRole, "title"},
        {ChipRole, "chip"},     {DetailRole, "detail"},     {ExtraRole, "extra"},
        {KindRole, "kind"},     {AccentRole, "accent"},     {TabRole, "tab"},
        {DiningSectionRole, "diningSection"}, {MenuMealRole, "menuMeal"}, {LastRole, "isLast"},
    };
}

int AgendaModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant AgendaModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const AgendaRow &row = m_rows.at(index.row());
    switch (role) {
    case TimeRole:
        return row.time;
    case MeridiemRole:
        return row.meridiem;
    case TitleRole:
        return row.title;
    case ChipRole:
        return row.chip;
    case DetailRole:
        return row.detail;
    case ExtraRole:
        return row.extra;
    case KindRole:
        return row.kind;
    case AccentRole:
        return row.accent;
    case TabRole:
        return row.tab;
    case DiningSectionRole:
        return row.diningSection;
    case MenuMealRole:
        return row.menuMeal;
    case LastRole:
        return index.row() == m_rows.size() - 1;
    default:
        return {};
    }
}

void AgendaModel::replace(const QList<AgendaRow> &rows)
{
    if (rows == m_rows)
        return;
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

// ---------------------------------------------------------------------------
// TodayViewModel
// ---------------------------------------------------------------------------

TodayViewModel::TodayViewModel(ChapelViewModel *chapel, DiningViewModel *dining, CurfewViewModel *curfew,
                               QObject *parent)
    : QObject(parent)
    , m_chapel(chapel)
    , m_dining(dining)
    , m_curfew(curfew)
    , m_now(QDateTime::currentDateTime())
    , m_agenda(this)
{
    // Everything here is derived, so anything a source says recomputes it.
    if (chapel)
        connect(chapel, &ChapelViewModel::changed, this, &TodayViewModel::rebuild);
    if (dining)
        connect(dining, &DiningViewModel::changed, this, &TodayViewModel::rebuild);
    rebuild();
}

QString TodayViewModel::dateText() const
{
    return fmt::dayLong(m_now.date()) + QStringLiteral(", ") + fmt::monthDay(m_now.date());
}

QString TodayViewModel::nowText() const
{
    return QStringLiteral("Now ") + fmt::clock(m_now.time());
}

QString TodayViewModel::chapelDateText() const
{
    return m_chapelStartsAt.isValid() ? fmt::shortDate(m_chapelStartsAt.date()) : QString();
}

QDate TodayViewModel::nextMorning() const
{
    return m_now.time() < QTime(5, 0) ? m_now.date() : m_now.date().addDays(1);
}

QString TodayViewModel::tomorrowDateText() const
{
    return fmt::shortDate(nextMorning());
}

QString TodayViewModel::morningTitle() const
{
    return nextMorning() == m_now.date() ? QStringLiteral("This morning") : QStringLiteral("Tomorrow morning");
}

void TodayViewModel::refreshAll()
{
    rebuild();
}

QVariantList TodayViewModel::snapshot() const
{
    return {m_heroKind, dateText(), nowText(), m_chapelSpeaker, m_chapelDescription, m_chapelTime,
            m_chapelCountdown, m_chapelStartsAt, m_chapelLivestream, m_chapelYoutubeId, m_mealLabel,
            m_mealStatus, m_mealLive, m_mealHoursText, m_mealItems, m_mealExtra, m_mealSlot, m_stillOpen,
            m_tomorrow, morningTitle(), tomorrowDateText()};
}

void TodayViewModel::rebuild()
{
    m_now = now();
    const QDate today = m_now.date();
    const int minute = m_now.time().hour() * 60 + m_now.time().minute();
    const QList<DayMenu> menus = m_dining ? m_dining->menus() : QList<DayMenu>();
    const QList<UpcomingChapel> chapels = m_chapel ? m_chapel->chapels() : QList<UpcomingChapel>();

    // ---- What leads
    const std::optional<UpcomingChapel> todays = chapelOn(chapels, today, m_now);
    const QList<Sitting> sittings = commonsSittings(diningDayOf(today));
    const bool commonsDone = sittings.isEmpty() || minute >= sittings.last().hours.close;
    // Past midnight on a Friday or Saturday night, the night is still last
    // night's.
    const bool lastNight = curfewNight(m_now) < today;
    if (todays)
        m_heroKind = QStringLiteral("chapel");
    else if (m_now.time() >= QTime(20, 0) || commonsDone || lastNight)
        m_heroKind = QStringLiteral("curfew");
    else
        m_heroKind = QStringLiteral("meal");

    // ---- The chapel hero
    if (todays) {
        m_chapelSpeaker = todays->who();
        m_chapelDescription = todays->description;
        m_chapelStartsAt = todays->startsAt.toLocalTime();
        m_chapelTime = fmt::clock(m_chapelStartsAt.time());
        m_chapelCountdown = isHappening(*todays, m_now) ? QStringLiteral("now")
                                                        : QStringLiteral("in ") + fmt::span(m_now.secsTo(m_chapelStartsAt));
        m_chapelLivestream = todays->willLivestream;
        m_chapelYoutubeId = todays->youtubeId;
    } else {
        m_chapelSpeaker.clear();
        m_chapelDescription.clear();
        m_chapelTime.clear();
        m_chapelCountdown.clear();
        m_chapelStartsAt = QDateTime();
        m_chapelLivestream = false;
        m_chapelYoutubeId.clear();
    }

    // ---- The meal hero: the sitting still to come or under way
    std::optional<Sitting> sitting;
    for (const Sitting &s : sittings) {
        if (s.hours.close > minute) {
            sitting = s;
            break;
        }
    }
    m_mealItems.clear();
    m_mealExtra.clear();
    m_mealLive = false;
    if (sitting) {
        const QDateTime opens = at(today, sitting->hours.open);
        const QDateTime closes = at(today, sitting->hours.close);
        m_mealLabel = sitting->name;
        m_mealSlot = sitting->slot;
        m_mealHoursText = fmt::clock(opens.time()) + QStringLiteral(" – ") + fmt::clock(closes.time());
        if (m_now >= opens) {
            m_mealStatus = QStringLiteral("Serving now");
            m_mealLive = true;
        } else if (m_now.secsTo(opens) <= 60 * 60) {
            m_mealStatus = QStringLiteral("Opens in ") + fmt::span(m_now.secsTo(opens));
            m_mealLive = true;
        } else {
            m_mealStatus = QStringLiteral("Opens at ") + fmt::clock(opens.time());
        }
        m_mealItems = names(itemsAt(menus, today, sitting->slot, HOME_COOKING), 6);
        m_mealExtra = gardenLine(menus, today, sitting->slot);
    } else {
        m_mealLabel.clear();
        m_mealSlot.clear();
        m_mealStatus.clear();
        m_mealHoursText.clear();
    }

    // ---- The rest of today
    QList<AgendaRow> rows;
    if (m_heroKind != u"curfew") {
        QStringList slotsSeen;
        if (m_heroKind == u"meal" && sitting)
            slotsSeen.append(sitting->slot);
        for (const Sitting &s : sittings) {
            if (s.hours.close <= minute || slotsSeen.contains(s.slot))
                continue;
            slotsSeen.append(s.slot);
            AgendaRow row;
            row.at = at(today, s.hours.open);
            row.title = s.name;
            row.detail = names(itemsAt(menus, today, s.slot, HOME_COOKING), 4).join(QStringLiteral(" · "));
            row.extra = gardenLine(menus, today, s.slot);
            row.kind = QStringLiteral("meal");
            row.tab = 2;
            row.diningSection = 1;
            row.menuMeal = s.slot;
            rows.append(row);
        }
        if (todays && m_heroKind != u"chapel" && todays->startsAt > m_now) {
            AgendaRow row;
            row.at = todays->startsAt.toLocalTime();
            row.title = QStringLiteral("Chapel · ") + todays->who();
            row.detail = todays->description;
            row.kind = QStringLiteral("chapel");
            row.tab = 1;
            rows.append(row);
        }
        AgendaRow curfewRow;
        curfewRow.at = curfewFor(curfewNight(m_now));
        curfewRow.title = QStringLiteral("Curfew");
        curfewRow.detail = fmt::dayLong(curfewNight(m_now)) + QStringLiteral(" night · residence halls");
        curfewRow.kind = QStringLiteral("curfew");
        curfewRow.tab = 3;
        rows.append(curfewRow);

        std::stable_sort(rows.begin(), rows.end(), [](const AgendaRow &a, const AgendaRow &b) { return a.at < b.at; });
        for (qsizetype i = 0; i < rows.size(); ++i) {
            AgendaRow &row = rows[i];
            row.time = fmt::clockBare(row.at.time());
            row.meridiem = meridiem(row.at.time());
            const bool next = i == 0 && row.at > m_now && m_now.secsTo(row.at) <= CHIP_WITHIN_SECS;
            if (next)
                row.chip = QStringLiteral("in ") + fmt::span(m_now.secsTo(row.at));
            row.accent = next ? QStringLiteral("gold")
                              : row.kind == u"meal" ? QStringLiteral("cedar") : QStringLiteral("faint");
        }
    }
    m_agenda.replace(rows);

    // ---- Night: what is still open, and tomorrow morning
    struct Open
    {
        QString name, detail, kind;
        QDateTime closes;
    };
    QList<Open> open;
    for (const HoursViewModel::OpenPlace &place : HoursViewModel::openDiningPlaces(m_now)) {
        const QString closes = QStringLiteral("closes ") + fmt::clock(place.closes.time());
        open.append({place.name,
                     place.note.isEmpty() || place.note.size() > 18 ? closes.left(1).toUpper() + closes.mid(1)
                                                                     : place.note + QStringLiteral(" · ") + closes,
                     QStringLiteral("dining"), place.closes});
    }
    for (const Building &building : buildings()) {
        if (const auto closes = closesAt([building](QDate d) { return building.on(d); }, m_now))
            open.append({building.name, QStringLiteral("Closes ") + fmt::clock(closes->time()),
                         QStringLiteral("building"), *closes});
    }
    std::stable_sort(open.begin(), open.end(), [](const Open &a, const Open &b) { return a.closes < b.closes; });
    m_stillOpen.clear();
    for (const Open &place : open) {
        if (m_stillOpen.size() >= 3)
            break;
        const qint64 left = m_now.secsTo(place.closes);
        m_stillOpen.append(QVariantMap{
            {QStringLiteral("name"), place.name},
            {QStringLiteral("detail"), place.detail},
            {QStringLiteral("closesIn"), fmt::span(left)},
            {QStringLiteral("soon"), left <= 30 * 60},
            {QStringLiteral("kind"), place.kind},
        });
    }

    m_tomorrow.clear();
    const QDate morning = nextMorning();
    const QList<Sitting> morningSittings = commonsSittings(diningDayOf(morning));
    if (!morningSittings.isEmpty()) {
        const Sitting &first = morningSittings.first();
        const QTime opens = timeOfDay(first.hours.open);
        m_tomorrow.append(QVariantMap{
            {QStringLiteral("time"), fmt::clockBare(opens)},
            {QStringLiteral("meridiem"), meridiem(opens)},
            {QStringLiteral("title"), first.name},
            {QStringLiteral("detail"),
             names(itemsAt(menus, morning, first.slot, HOME_COOKING), 4).join(QStringLiteral(" · "))},
            {QStringLiteral("kind"), QStringLiteral("meal")},
            {QStringLiteral("tab"), 2},
            {QStringLiteral("diningSection"), 1},
            {QStringLiteral("menuMeal"), first.slot},
            {QStringLiteral("menuDay"), static_cast<int>(today.daysTo(morning))},
        });
    }
    if (const auto chapel = chapelOn(chapels, morning, m_now)) {
        const QTime starts = chapel->startsAt.toLocalTime().time();
        m_tomorrow.append(QVariantMap{
            {QStringLiteral("time"), fmt::clockBare(starts)},
            {QStringLiteral("meridiem"), meridiem(starts)},
            {QStringLiteral("title"), QStringLiteral("Chapel · ") + chapel->who()},
            {QStringLiteral("detail"), chapel->description},
            {QStringLiteral("kind"), QStringLiteral("chapel")},
            {QStringLiteral("tab"), 1},
            {QStringLiteral("diningSection"), -1},
            {QStringLiteral("menuMeal"), QString()},
            {QStringLiteral("menuDay"), 0},
        });
    }

    // Rebuilt on every tick and every source change; announced only when
    // something QML shows is different, so the lists bound to it do not
    // rebuild for nothing.
    QVariantList published = snapshot();
    if (published != m_published) {
        m_published = std::move(published);
        emit changed();
    }
}

} // namespace mycu
