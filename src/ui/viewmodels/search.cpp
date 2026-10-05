#include "search.h"

#include "chapel.h"
#include "core/hours.h"
#include "core/providers/chapel_schedule.h"
#include "core/providers/dining.h"
#include "dining.h"
#include "format.h"
#include "ui/settings.h"

#include <QSet>

#include <algorithm>

namespace mycu {

using namespace hours;

namespace search {

QStringList tokens(const QString &text)
{
    // Decompose, then drop the accents: "ñ" is "n" plus a combining tilde.
    const QString decomposed = text.normalized(QString::NormalizationForm_KD);
    QString folded;
    folded.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() == QChar::Mark_NonSpacing)
            continue;
        folded.append(c.isLetterOrNumber() ? c : QChar(u' '));
    }
    return folded.toCaseFolded().split(u' ', Qt::SkipEmptyParts);
}

QStringList queryTokens(const QString &query)
{
    static const QSet<QString> filler = {
        QStringLiteral("a"),    QStringLiteral("an"),   QStringLiteral("the"),   QStringLiteral("at"),
        QStringLiteral("of"),   QStringLiteral("for"),  QStringLiteral("is"),    QStringLiteral("when"),
        QStringLiteral("does"), QStringLiteral("what"), QStringLiteral("where"), QStringLiteral("s"),
        QStringLiteral("hours"), QStringLiteral("hour"),
    };
    QStringList out;
    for (const QString &token : tokens(query)) {
        if (!filler.contains(token))
            out.append(token);
    }
    return out;
}

bool matches(const QStringList &query, const QString &text)
{
    if (query.isEmpty())
        return false;
    const QStringList haystack = tokens(text);
    return std::all_of(query.cbegin(), query.cend(), [&](const QString &needle) {
        return std::any_of(haystack.cbegin(), haystack.cend(),
                           [&](const QString &token) { return token.startsWith(needle); });
    });
}

} // namespace search

// ---------------------------------------------------------------------------
// SearchResultModel
// ---------------------------------------------------------------------------

QHash<int, QByteArray> SearchResultModel::roleNames() const
{
    return {
        {RowTypeRole, "rowType"}, {SectionRole, "section"}, {TitleRole, "title"},
        {DetailRole, "detail"},   {BadgeRole, "badge"},     {HotRole, "hot"},
        {IconRole, "icon"},       {TabRole, "tab"},         {DiningSectionRole, "diningSection"},
        {MenuDayRole, "menuDay"}, {MenuMealRole, "menuMeal"},
    };
}

int SearchResultModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant SearchResultModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case RowTypeRole:
        return row.rowType;
    case SectionRole:
        return row.section;
    case TitleRole:
        return row.title;
    case DetailRole:
        return row.detail;
    case BadgeRole:
        return row.badge;
    case HotRole:
        return row.hot;
    case IconRole:
        return row.icon;
    case TabRole:
        return row.tab;
    case DiningSectionRole:
        return row.diningSection;
    case MenuDayRole:
        return row.menuDay;
    case MenuMealRole:
        return row.menuMeal;
    default:
        return {};
    }
}

void SearchResultModel::replace(const QList<Row> &rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

// ---------------------------------------------------------------------------
// SearchViewModel
// ---------------------------------------------------------------------------

namespace {

constexpr int MENU_LIMIT = 12;
constexpr int CHAPEL_LIMIT = 8;

QString dayWord(QDate day, QDate today)
{
    if (day == today)
        return QStringLiteral("today");
    if (day == today.addDays(1))
        return QStringLiteral("tomorrow");
    return fmt::dayShort(day);
}

QTime timeOf(int minute)
{
    return QTime(0, 0).addSecs((minute % 1440) * 60);
}

// "Open until 11 PM" / "Opens 8 PM" / "Closed today", and whether it is open.
std::pair<QString, bool> openState(const Schedule &schedule, const QDateTime &now)
{
    if (const auto closes = closesAt(schedule, now))
        return {QStringLiteral("Until ") + fmt::clockShort(closes->time()), true};
    if (const auto next = nextOpening(schedule, now, 0))
        return {QStringLiteral("Opens ") + fmt::clockShort(next->opens.time()), false};
    return {QStringLiteral("Closed today"), false};
}

// "8:00 – 11:00 PM" for today's windows, or "Closed today".
QString todaysHours(const DayWindows &windows)
{
    if (windows.isEmpty())
        return QStringLiteral("Closed today");
    return fmt::clock(timeOf(windows.first().open)) + QStringLiteral(" – ")
        + fmt::clock(timeOf(windows.last().close));
}

} // namespace

SearchViewModel::SearchViewModel(ChapelViewModel *chapel, DiningViewModel *dining, SettingsController *settings,
                                 QObject *parent)
    : QObject(parent)
    , m_chapel(chapel)
    , m_dining(dining)
    , m_settings(settings)
    , m_results(this)
{
    // New data while a query is up re-runs it, so a menu landing behind the
    // search page shows up in it. Only new data: a clock tick would reset the
    // results under the reader's thumb.
    auto rerun = [this] {
        if (!m_query.isEmpty())
            rebuild();
    };
    if (chapel)
        connect(chapel->scheduleStatus(), &SourceStatus::loaded, this, rerun);
    if (dining)
        connect(dining->menuStatus(), &SourceStatus::loaded, this, rerun);
    if (settings)
        connect(settings, &SettingsController::changed, this, &SearchViewModel::changed);
}

QStringList SearchViewModel::suggestions() const
{
    return {QStringLiteral("Library"), QStringLiteral("Pizza"), QStringLiteral("Chick-fil-A"),
            QStringLiteral("Fitness center")};
}

QStringList SearchViewModel::recent() const
{
    return m_settings ? m_settings->recentSearches() : QStringList();
}

void SearchViewModel::setQuery(const QString &query)
{
    if (query == m_query)
        return;
    m_query = query;
    rebuild();
}

void SearchViewModel::setScope(int scope)
{
    scope = std::clamp(scope, 0, 3);
    m_scope = scope == m_scope ? 0 : scope;
    publish();
}

void SearchViewModel::commit()
{
    if (m_settings)
        m_settings->addRecentSearch(m_query);
}

void SearchViewModel::clearRecent()
{
    if (m_settings)
        m_settings->clearRecentSearches();
}

void SearchViewModel::rebuild()
{
    const QStringList query = search::queryTokens(m_query);
    m_places.clear();
    m_menu.clear();
    m_chapels.clear();
    m_placeAnswer.clear();
    m_menuAnswer.clear();
    m_chapelAnswer.clear();
    m_placeByName = false;
    if (!query.isEmpty()) {
        findPlaces(query);
        findMenu(query);
        findChapels(query);
    }
    publish();
}

void SearchViewModel::findPlaces(const QStringList &query)
{
    const QDateTime current = now();
    auto add = [&](const QString &name, const QString &haystack, const QString &note, const Schedule &schedule,
                   int tab, int section, const QString &icon) {
        const bool byName = search::matches(query, name);
        if (!byName && !search::matches(query, haystack))
            return;
        const auto [badge, open] = openState(schedule, current);
        SearchResultModel::Row row;
        row.rowType = QStringLiteral("item");
        row.section = QStringLiteral("places");
        row.title = name;
        const QString hoursToday = todaysHours(schedule(current.date()));
        row.detail = note.isEmpty() ? hoursToday : note + QStringLiteral(" · ") + hoursToday;
        row.badge = badge;
        row.hot = open;
        row.icon = icon;
        row.tab = tab;
        row.diningSection = section;
        m_places.append(row);

        if (m_placeAnswer.isEmpty() || (byName && !m_placeByName)) {
            m_placeByName = byName;
            m_placeAnswer = {
                {QStringLiteral("kicker"), open ? QStringLiteral("Open now") : QStringLiteral("Closed now")},
                {QStringLiteral("tag"), QString()},
                {QStringLiteral("headline"), name},
                {QStringLiteral("chips"), QStringList()},
                {QStringLiteral("footer"), badge + QStringLiteral(" · ") + hoursToday},
                {QStringLiteral("tab"), tab},
                {QStringLiteral("diningSection"), section},
                {QStringLiteral("menuDay"), 0},
                {QStringLiteral("menuMeal"), QString()},
            };
        }
    };

    QString sittings;
    for (const Sitting &sitting : commonsSittings(diningDayOf(current.date())))
        sittings += u' ' + sitting.name;
    add(QStringLiteral("The Commons"), QStringLiteral("The Commons dining hall Chuck's") + sittings, QString(),
        commonsOn, 2, 2, QStringLiteral("dining"));
    for (const Venue &venue : venues()) {
        add(venue.name, venue.name + u' ' + venue.note, venue.note, [venue](QDate d) { return venue.on(d); }, 2, 2,
            QStringLiteral("dining"));
    }
    for (const Building &building : buildings()) {
        add(building.name, building.name + u' ' + building.code, building.code,
            [building](QDate d) { return building.on(d); }, 3, -1, QStringLiteral("campus"));
    }
}

void SearchViewModel::findMenu(const QStringList &query)
{
    if (!m_dining)
        return;
    const QDateTime current = now();
    const QDate today = current.date();

    // The first time each dish is served from now on.
    QSet<QString> seen;
    for (const DayMenu &day : m_dining->menus()) {
        if (day.on < today)
            continue;
        QList<MenuBlock> blocks = day.blocks;
        std::stable_sort(blocks.begin(), blocks.end(),
                         [](const MenuBlock &a, const MenuBlock &b) { return a.sortKey() < b.sortKey(); });
        for (const MenuBlock &block : blocks) {
            const auto hours = servingHours(day.on, block.slot);
            // A sitting already over today is not "where to get it".
            if (hours && day.on == today && current.time() >= hours->second)
                continue;
            for (const MenuItem &item : block.items) {
                if (!search::matches(query, item.name))
                    continue;
                const QString key = search::tokens(item.name).join(u' ');
                if (seen.contains(key))
                    continue;
                seen.insert(key);

                const bool allDay = !hours;
                const QString when = allDay ? QStringLiteral("all day ") + dayWord(day.on, today)
                                            : SLOT_LABELS.value(block.slot.toCaseFolded()).toLower() + u' '
                                                  + dayWord(day.on, today);
                const bool servingNow = day.on == today
                    && (allDay ? isOpen(commonsOn, current)
                               : current.time() >= hours->first && current.time() < hours->second);
                SearchResultModel::Row row;
                row.rowType = QStringLiteral("item");
                row.section = QStringLiteral("menu");
                row.title = item.name;
                row.detail = block.venue + QStringLiteral(" · ") + when;
                row.badge = servingNow ? QStringLiteral("Now") : QString();
                row.hot = servingNow;
                row.icon = QStringLiteral("plate");
                row.tab = 2;
                row.diningSection = 1;
                row.menuDay = static_cast<int>(today.daysTo(day.on));
                row.menuMeal = allDay ? QString() : block.slot.toCaseFolded();
                if (m_menu.size() < MENU_LIMIT)
                    m_menu.append(row);

                if (m_menuAnswer.isEmpty()) {
                    // Every dish at that station that day the query also finds.
                    QStringList chips;
                    for (const MenuItem &other : block.items) {
                        if (search::matches(query, other.name) && chips.size() < 4)
                            chips.append(other.name);
                    }
                    QString footer;
                    if (allDay) {
                        if (const auto closes = closesAt(commonsOn, current))
                            footer = QStringLiteral("The Commons is open until ") + fmt::clock(closes->time());
                        else if (const auto next = nextOpening(commonsOn, current))
                            footer = QStringLiteral("The Commons opens ")
                                + (next->opens.date() == current.date() ? QStringLiteral("at ")
                                                                        : fmt::dayLong(next->opens.date())
                                                                              + QStringLiteral(" at "))
                                + fmt::clock(next->opens.time());
                    } else {
                        footer = SLOT_LABELS.value(block.slot.toCaseFolded()) + u' ' + fmt::clock(hours->first)
                            + QStringLiteral(" – ") + fmt::clock(hours->second);
                        const QDateTime starts(day.on, hours->first);
                        if (starts > current && day.on == today)
                            footer += QStringLiteral(" · in ") + fmt::span(current.secsTo(starts));
                    }
                    m_menuAnswer = {
                        {QStringLiteral("kicker"), (day.on == today ? QStringLiteral("Today")
                                                   : day.on == today.addDays(1) ? QStringLiteral("Tomorrow")
                                                                                : fmt::dayLong(day.on))
                                                       + QStringLiteral(" · The Commons")},
                        {QStringLiteral("tag"),
                         allDay ? QStringLiteral("All day") : SLOT_LABELS.value(block.slot.toCaseFolded())},
                        {QStringLiteral("headline"), QStringLiteral("At the %1 station").arg(block.venue)},
                        {QStringLiteral("chips"), chips},
                        {QStringLiteral("footer"), footer},
                        {QStringLiteral("tab"), 2},
                        {QStringLiteral("diningSection"), 1},
                        {QStringLiteral("menuDay"), row.menuDay},
                        {QStringLiteral("menuMeal"), row.menuMeal},
                    };
                }
            }
        }
    }
}

void SearchViewModel::findChapels(const QStringList &query)
{
    if (!m_chapel)
        return;
    const QDateTime current = now();
    for (const UpcomingChapel &chapel : m_chapel->chapels()) {
        if (!chapel.startsAt.isValid() || isOver(chapel, current))
            continue;
        if (!search::matches(query, chapel.who() + u' ' + chapel.title))
            continue;
        const QDateTime when = chapel.startsAt.toLocalTime();
        SearchResultModel::Row row;
        row.rowType = QStringLiteral("item");
        row.section = QStringLiteral("chapel");
        row.title = chapel.who();
        row.detail = fmt::shortDate(when.date()) + QStringLiteral(" · ") + fmt::clock(when.time());
        row.badge = when.date() == current.date()           ? QStringLiteral("Today")
                    : when.date() == current.date().addDays(1) ? QStringLiteral("Tomorrow")
                                                               : QString();
        row.hot = when.date() == current.date();
        row.icon = QStringLiteral("chapel");
        row.tab = 1;
        if (m_chapels.size() < CHAPEL_LIMIT)
            m_chapels.append(row);

        if (m_chapelAnswer.isEmpty()) {
            m_chapelAnswer = {
                {QStringLiteral("kicker"), QStringLiteral("Chapel")},
                {QStringLiteral("tag"), chapel.willLivestream ? QStringLiteral("Livestreamed") : QString()},
                {QStringLiteral("headline"), chapel.who()},
                {QStringLiteral("chips"), QStringList()},
                {QStringLiteral("footer"), row.detail},
                {QStringLiteral("tab"), 1},
                {QStringLiteral("diningSection"), -1},
                {QStringLiteral("menuDay"), 0},
                {QStringLiteral("menuMeal"), QString()},
            };
        }
    }
}

void SearchViewModel::publish()
{
    // A place named by the query outranks a dish; otherwise what is being
    // served is the likelier question.
    if (m_placeByName)
        m_topAnswer = m_placeAnswer;
    else if (!m_menuAnswer.isEmpty())
        m_topAnswer = m_menuAnswer;
    else if (!m_chapelAnswer.isEmpty())
        m_topAnswer = m_chapelAnswer;
    else
        m_topAnswer = m_placeAnswer;
    if (m_scope != 0) {
        const int tab = m_topAnswer.value(QStringLiteral("tab")).toInt();
        const int section = m_topAnswer.value(QStringLiteral("diningSection")).toInt();
        const bool inScope = (m_scope == 1 && tab == 2 && section == 1) || (m_scope == 3 && tab == 1)
            || (m_scope == 2 && (tab == 3 || (tab == 2 && section == 2)));
        if (!inScope)
            m_topAnswer.clear();
    }

    QList<SearchResultModel::Row> rows;
    auto section = [&](int scope, const QString &key, const QString &title,
                       const QList<SearchResultModel::Row> &items) {
        if (items.isEmpty() || (m_scope != 0 && m_scope != scope))
            return;
        SearchResultModel::Row header;
        header.rowType = QStringLiteral("section");
        header.section = key;
        header.title = title;
        rows.append(header);
        rows.append(items);
    };
    section(2, QStringLiteral("places"), QStringLiteral("Places"), m_places);
    section(1, QStringLiteral("menu"), QStringLiteral("Menu"), m_menu);
    section(3, QStringLiteral("chapel"), QStringLiteral("Chapel"), m_chapels);
    m_results.replace(rows);
    emit changed();
}

} // namespace mycu
