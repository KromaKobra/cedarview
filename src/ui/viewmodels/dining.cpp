#include "dining.h"

#include "core/calendar.h"
#include "core/errors.h"
#include "core/hours.h"
#include "core/log.h"
#include "format.h"
#include "ui/settings.h"
#include "ui/tasks.h"

#include <QElapsedTimer>
#include <QMap>

#include <algorithm>

namespace mycu {

namespace {

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

QString stationKind(const QString &station)
{
    if (station == GARDEN_BITES)
        return QStringLiteral("garden");
    if (station == ALLERGEN_AWARE)
        return QStringLiteral("aware");
    return QStringLiteral("home");
}

bool avoided(const MenuItem &item, const QSet<QString> &avoid)
{
    return std::any_of(item.allergens.cbegin(), item.allergens.cend(),
                       [&](const QString &a) { return avoid.contains(a.toLower()); });
}

// "Today" / "Yesterday" / "Mon, Sep 14".
QString dayLabel(QDate day, QDate today)
{
    if (!day.isValid())
        return QStringLiteral("Date unknown");
    if (day == today)
        return QStringLiteral("Today");
    if (day == today.addDays(-1))
        return QStringLiteral("Yesterday");
    return fmt::shortDate(day);
}

QString menuErrorText(std::exception_ptr error)
{
    try {
        std::rethrow_exception(error);
    } catch (const ParseError &) {
        return QStringLiteral("The dining menu feed changed shape.");
    } catch (const TransportError &e) {
        return QStringLiteral("Couldn't reach the dining menu service: ") + e.message();
    } catch (...) {
        return QStringLiteral("Unexpected error: ") + describe(error);
    }
}

QString count(qsizetype n, const QString &noun)
{
    return QString::number(n) + u' ' + noun + (n == 1 ? QString() : QStringLiteral("s"));
}

QDate firstOf(const QSet<QDate> &dates)
{
    return dates.isEmpty() ? QDate() : *std::min_element(dates.cbegin(), dates.cend());
}

QString capitalised(const QString &text)
{
    return text.isEmpty() ? text : text.left(1).toUpper() + text.mid(1);
}

// "10:30 AM – 2:30 PM".
QString range(QTime start, QTime end)
{
    return fmt::clock(start) + QStringLiteral(" – ") + fmt::clock(end);
}

bool hasFeatured(const DayMenu &day)
{
    return std::any_of(day.blocks.cbegin(), day.blocks.cend(), [](const MenuBlock &b) {
        return !b.items.isEmpty() && FEATURED_STATIONS.contains(b.venue, Qt::CaseInsensitive);
    });
}

QString activityKind(const MealTransaction &t)
{
    if (t.isDeposit)
        return QStringLiteral("deposit");
    if (t.isFlex())
        return QStringLiteral("flex");
    if (t.activity.contains(QStringLiteral("exchange"), Qt::CaseInsensitive))
        return QStringLiteral("exchange");
    return QStringLiteral("meal");
}

} // namespace

// ---------------------------------------------------------------------------
// MenuListModel
// ---------------------------------------------------------------------------

QHash<int, QByteArray> MenuListModel::roleNames() const
{
    return {{TextRole, "text"}, {AllergenRole, "allergens"}, {NewRole, "isNew"}};
}

int MenuListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant MenuListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const MenuItem &item = m_items.at(index.row());
    switch (role) {
    case TextRole:
        return item.name;
    case AllergenRole:
        return item.allergenText();
    case NewRole:
        return item.isNew;
    default:
        return {};
    }
}

void MenuListModel::replaceItems(const QList<MenuItem> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
}

// ---------------------------------------------------------------------------
// StationListModel
// ---------------------------------------------------------------------------

QHash<int, QByteArray> StationListModel::roleNames() const
{
    return {
        {RowTypeRole, "rowType"}, {StationRole, "station"},       {StationKindRole, "stationKind"},
        {TextRole, "text"},       {AllergenRole, "allergens"},    {NewRole, "isNew"},
        {HiddenTextRole, "hiddenText"}, {LastRole, "isLast"},
    };
}

int StationListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant StationListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case RowTypeRole:
        return row.rowType;
    case StationRole:
        return row.station;
    case StationKindRole:
        return row.stationKind;
    case TextRole:
        return row.text;
    case AllergenRole:
        return row.allergens;
    case NewRole:
        return row.isNew;
    case HiddenTextRole:
        return row.hiddenText;
    case LastRole:
        return row.isLast;
    default:
        return {};
    }
}

int StationListModel::replace(const QList<MenuBlock> &blocks, const QSet<QString> &avoid)
{
    int hiddenTotal = 0;
    QList<Row> rows;
    for (const QString &station : FEATURED_STATIONS) {
        QList<MenuItem> items;
        for (const MenuBlock &block : blocks) {
            if (block.venue.compare(station, Qt::CaseInsensitive) == 0)
                items.append(block.items);
        }
        if (items.isEmpty())
            continue;

        const QString kind = stationKind(station);
        // Garden Bites lists its dish first and then the bar that goes with
        // it (butter, sour cream, the potatoes themselves); the bar is one
        // line, not seven rows.
        const bool hasBar = station == GARDEN_BITES && items.size() > 1;

        Row header;
        header.rowType = QStringLiteral("header");
        header.station = station;
        header.stationKind = kind;
        rows.append(header);
        const qsizetype headerAt = rows.size() - 1;

        int hidden = 0;
        int shown = 0;
        QStringList bar;
        for (qsizetype i = 0; i < items.size(); ++i) {
            const MenuItem &item = items.at(i);
            if (avoided(item, avoid)) {
                ++hidden;
                continue;
            }
            ++shown;
            if (hasBar && i > 0) {
                bar.append(item.name);
                continue;
            }
            Row row;
            row.rowType = QStringLiteral("item");
            row.station = station;
            row.stationKind = kind;
            row.text = item.name;
            row.allergens = item.allergenText();
            row.isNew = item.isNew;
            rows.append(row);
        }
        if (!bar.isEmpty()) {
            const bool potatoes = std::any_of(bar.cbegin(), bar.cend(), [](const QString &name) {
                return name.contains(QStringLiteral("potato"), Qt::CaseInsensitive);
            });
            Row row;
            row.rowType = QStringLiteral("extras");
            row.station = station;
            row.stationKind = kind;
            row.text = (potatoes ? QStringLiteral("Potato bar: ") : QStringLiteral("Also: "))
                + bar.join(QStringLiteral(", "));
            rows.append(row);
        }
        if (shown == 0) {
            Row row;
            row.rowType = QStringLiteral("note");
            row.station = station;
            row.stationKind = kind;
            row.text = QStringLiteral("Everything here has something you're avoiding.");
            rows.append(row);
        }
        if (hidden > 0)
            rows[headerAt].hiddenText = QStringLiteral("%1 hidden").arg(hidden);
        rows.last().isLast = true;
        hiddenTotal += hidden;
    }

    beginResetModel();
    m_rows = rows;
    endResetModel();
    return hiddenTotal;
}

// ---------------------------------------------------------------------------
// ActivityListModel
// ---------------------------------------------------------------------------

QHash<int, QByteArray> ActivityListModel::roleNames() const
{
    return {
        {HeaderRole, "isHeader"}, {TitleRole, "title"},   {DetailRole, "detail"}, {AmountRole, "amount"},
        {FlexRole, "isFlex"},     {DepositRole, "isDeposit"}, {KindRole, "kind"},
    };
}

int ActivityListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant ActivityListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case HeaderRole:
        return row.isHeader;
    case TitleRole:
        return row.title;
    case DetailRole:
        return row.detail;
    case AmountRole:
        return row.amount;
    case FlexRole:
        return row.isFlex;
    case DepositRole:
        return row.isDeposit;
    case KindRole:
        return row.kind;
    default:
        return {};
    }
}

void ActivityListModel::replace(const QList<MealTransaction> &transactions, QDate today)
{
    beginResetModel();
    m_rows.clear();
    qsizetype headerAt = -1;
    qsizetype meals = 0;
    double spent = 0.0;
    // The day's sum goes on its header, once the day is done.
    auto closeDay = [&] {
        if (headerAt < 0)
            return;
        QStringList parts;
        if (meals > 0)
            parts.append(count(meals, QStringLiteral("meal")));
        if (spent > 0.0)
            parts.append(MealPlan::money(spent));
        m_rows[headerAt].detail = parts.join(QStringLiteral(" · "));
    };

    QDate lastDay;
    for (const MealTransaction &t : transactions) {
        const QDate day = t.at.isValid() ? t.at.date() : QDate();
        if (headerAt < 0 || day != lastDay) {
            closeDay();
            Row header;
            header.isHeader = true;
            header.title = dayLabel(day, today);
            m_rows.append(header);
            headerAt = m_rows.size() - 1;
            meals = 0;
            spent = 0.0;
            lastDay = day;
        }
        QStringList parts;
        if (!t.mealPeriod.isEmpty())
            parts.append(t.mealPeriod);
        if (t.at.isValid())
            parts.append(fmt::clock(t.at.time()));
        if (!t.isFlex())
            ++meals;
        if (t.amount && !t.isDeposit)
            spent += std::fabs(*t.amount);
        m_rows.append({false, t.activity, parts.join(QStringLiteral(" · ")), t.amountText(), t.isFlex(),
                       t.isDeposit, activityKind(t)});
    }
    closeDay();
    endResetModel();
}

// ---------------------------------------------------------------------------
// Flex pace
// ---------------------------------------------------------------------------

std::optional<FlexPace> flexPace(const MealPlan &plan, const QDateTime &now)
{
    if (!plan.diningDollars)
        return std::nullopt;
    const QDate today = now.date();
    const std::optional<TermWindow> term = currentTerm(today);
    if (!term)
        return std::nullopt;

    const QDate monday = today.addDays(-(today.dayOfWeek() - 1));
    double spent = 0.0;
    for (const MealTransaction &t : plan.transactions) {
        if (t.amount && !t.isDeposit && t.at.isValid() && t.at.date() >= monday && t.at <= now)
            spent += std::fabs(*t.amount);
    }
    const QDate ends = term->endIn(today.year());
    const double weeks = std::max(1.0, (monday.daysTo(ends) + 1) / 7.0);
    return FlexPace{(*plan.diningDollars + spent) / weeks, spent, ends};
}

// ---------------------------------------------------------------------------
// DiningViewModel
// ---------------------------------------------------------------------------

DiningViewModel::DiningViewModel(TransportPtr transport, std::optional<Storage> storage, int days, QObject *parent)
    : QObject(parent)
    , m_transport(std::move(transport))
    , m_storage(std::move(storage))
    , m_days(days)
    , m_stations(this)
    , m_nextMeal(this)
    , m_activity(this)
{
    fetchMenus = [this](DiningProvider provider, MenusDone done, MenusFailed failed) {
        runInBackground(
            this,
            [provider]() mutable {
                QElapsedTimer timer;
                timer.start();
                const QJsonValue payload = provider.fetchPayload();
                qCDebug(lcDining) << "dining: fetched" << provider.path() << "in" << timer.elapsed() << "ms";
                return std::make_pair(parseMenus(payload), payload);
            },
            [done](const std::pair<QList<DayMenu>, QJsonValue> &result) { done(result.first, result.second); },
            failed);
    };
    m_menuStatus.now = [this] { return now(); };
    m_planStatus.now = [this] { return now(); };
    connect(this, &DiningViewModel::changed, this, &DiningViewModel::clockChanged);

    m_nextMealTimer.setInterval(NEXT_MEAL_CHECK_MS);
    connect(&m_nextMealTimer, &QTimer::timeout, this, &DiningViewModel::tick);
    m_nextMealTimer.start();

    hydrate();
}

void DiningViewModel::attachSettings(SettingsController *settings)
{
    m_settings = settings;
    if (settings) {
        m_avoid = settings->avoid();
        rebuildStations();
        emit changed();
    }
}

void DiningViewModel::resetProvider(const MealsTarget &target)
{
    m_mealsProvider = std::make_shared<MealsProvider>(m_transport, target);
}

void DiningViewModel::hydrate()
{
    // What is shown is rebuilt, but nothing is fetched from here: when to
    // fetch is SyncCoordinator's call.
    if (!persisting()) {
        resetProvider({});
        rebuildStations();
        rebuildNextMeal();
        return;
    }

    const SessionState state = m_storage->session.load();
    resetProvider({state.mealsPersonId, state.mealsCard});

    // Parsed again from what was saved, by the parsers a fetch uses; a copy
    // they no longer accept is simply not shown.
    if (const auto saved = m_storage->cache.load(cachekey::MENUS)) {
        try {
            applyMenus(parseMenus(saved->payload));
            m_menuStatus.restored(saved->savedAt);
        } catch (const std::exception &e) {
            qCWarning(lcDining) << "dining: the saved menu is unreadable (ignored):" << e.what();
        }
    }
    if (const auto saved = m_storage->cache.load(cachekey::MEALS)) {
        try {
            m_plan = parseBalance(saved->payload.toObject());
            m_planStatus.restored(saved->savedAt);
            m_activity.replace(shownActivity(), now().date());
        } catch (const std::exception &e) {
            qCWarning(lcDining) << "dining: the saved meal plan is unreadable (ignored):" << e.what();
        }
    }
    rebuildStations();
    rebuildNextMeal();
}

QDate DiningViewModel::selectedDate() const
{
    return now().date().addDays(m_offset);
}

QString DiningViewModel::dateText() const
{
    if (m_offset == 0)
        return QStringLiteral("Today");
    if (m_offset == 1)
        return QStringLiteral("Tomorrow");
    if (m_offset == -1)
        return QStringLiteral("Yesterday");
    const QDate day = selectedDate();
    return fmt::dayShort(day) + u' ' + fmt::monthDay(day);
}

QString DiningViewModel::dateDetail() const
{
    const QDate day = selectedDate();
    QString text = fmt::dayLong(day) + QStringLiteral(", ") + fmt::monthLong(day) + u' '
        + QString::number(day.day());
    if (day.year() != now().date().year())
        text += QStringLiteral(", ") + QString::number(day.year());
    return text;
}

bool DiningViewModel::dayLoading() const
{
    const QDate target = selectedDate();
    return m_pending.contains(target) || (m_busy && !m_byDate.contains(target));
}

QString DiningViewModel::dayEmptyText() const
{
    if (m_stations.rowCount())
        return {};
    if (dayLoading())
        return QStringLiteral("Loading the menu…");
    if (!m_dayError.isEmpty())
        return m_dayError;
    const auto day = m_byDate.constFind(selectedDate());
    if (day == m_byDate.cend() && !m_error.isEmpty())
        return m_error;
    if (day != m_byDate.cend() && hasFeatured(*day))
        return QStringLiteral("Nothing posted for %1 on this day.").arg(SLOT_LABELS.value(m_meal).toLower());
    return QStringLiteral("Nothing posted for this day.");
}

QVariantList DiningViewModel::days() const
{
    const QDate today = now().date();
    QVariantList out;
    for (int offset = -3; offset <= 3; ++offset) {
        const QDate day = today.addDays(offset);
        const auto fetched = m_byDate.constFind(day);
        out.append(QVariantMap{
            {QStringLiteral("offset"), offset},
            {QStringLiteral("dow"), fmt::dayShort(day)},
            {QStringLiteral("num"), QString::number(day.day())},
            {QStringLiteral("isToday"), offset == 0},
            {QStringLiteral("selected"), offset == m_offset},
            {QStringLiteral("available"), fetched == m_byDate.cend() || hasFeatured(*fetched)},
        });
    }
    return out;
}

QString DiningViewModel::autoMeal() const
{
    const QDateTime current = now();
    for (const QString &slot : SLOT_ORDER) {
        const auto hours = servingHours(current.date(), slot);
        if (hours && current.time() < hours->second)
            return slot;
    }
    return QStringLiteral("dinner");
}

QVariantList DiningViewModel::mealTabs() const
{
    QVariantList out;
    for (const QString &slot : SLOT_ORDER) {
        const auto hours = servingHours(selectedDate(), slot);
        out.append(QVariantMap{
            {QStringLiteral("slot"), slot},
            {QStringLiteral("label"), SLOT_LABELS.value(slot)},
            {QStringLiteral("hours"),
             hours ? fmt::clockBare(hours->first) + QStringLiteral("–") + fmt::clockBare(hours->second) : QString()},
            {QStringLiteral("selected"), slot == m_meal},
        });
    }
    return out;
}

QString DiningViewModel::mealStatusText() const
{
    const QDate day = selectedDate();
    const auto hours = servingHours(day, m_meal);
    if (!hours)
        return {};
    const QDateTime current = now();
    const QDate today = current.date();

    if (day == today) {
        const QDateTime start(day, hours->first);
        const QDateTime end(day, hours->second);
        if (current < start) {
            const qint64 secs = current.secsTo(start);
            if (secs <= 90 * 60)
                return QStringLiteral("Opens in ") + fmt::span(secs) + QStringLiteral(" · ")
                    + range(hours->first, hours->second);
            return (m_meal == u"dinner" ? QStringLiteral("Tonight · ") : QStringLiteral("Later today · "))
                + range(hours->first, hours->second);
        }
        if (current < end)
            return QStringLiteral("Serving now · until ") + fmt::clock(hours->second);
        return QStringLiteral("Ended at ") + fmt::clock(hours->second);
    }

    QString when;
    if (day == today.addDays(1))
        when = QStringLiteral("Tomorrow");
    else if (day == today.addDays(-1))
        when = QStringLiteral("Yesterday");
    else
        when = fmt::shortDate(day);
    return when + QStringLiteral(" · ") + range(hours->first, hours->second);
}

bool DiningViewModel::mealStatusLive() const
{
    const QDate day = selectedDate();
    const QDateTime current = now();
    const auto hours = servingHours(day, m_meal);
    if (day != current.date() || !hours)
        return false;
    const QDateTime start(day, hours->first);
    const QDateTime end(day, hours->second);
    return current < end && current.secsTo(start) <= 60 * 60;
}

QVariantList DiningViewModel::avoidOptions() const
{
    QVariantList out;
    for (const QString &key : AVOIDABLE) {
        out.append(QVariantMap{{QStringLiteral("key"), key},
                               {QStringLiteral("label"), capitalised(key)},
                               {QStringLiteral("on"), m_avoid.contains(key)}});
    }
    return out;
}

QStringList DiningViewModel::avoid() const
{
    return m_avoid;
}

QSet<QString> DiningViewModel::avoidSet() const
{
    return QSet<QString>(m_avoid.cbegin(), m_avoid.cend());
}

QString DiningViewModel::hiddenText() const
{
    if (m_hidden <= 0)
        return {};
    return QStringLiteral("Hiding %1 with %2")
        .arg(count(m_hidden, QStringLiteral("item")), m_avoid.join(QStringLiteral(", ")));
}

QVariantList DiningViewModel::allDayStations() const
{
    const auto day = m_byDate.constFind(selectedDate());
    if (day == m_byDate.cend())
        return {};

    // In first-seen order, a station's blocks merged (Bake Shoppe is listed
    // both all day and at breakfast).
    QStringList order;
    QHash<QString, QStringList> items;
    for (const MenuBlock &block : day->blocks) {
        if (FEATURED_STATIONS.contains(block.venue, Qt::CaseInsensitive) || block.items.isEmpty())
            continue;
        const QString slot = block.slot.toCaseFolded();
        if (slot != u"anytime" && slot != m_meal)
            continue;
        if (!order.contains(block.venue))
            order.append(block.venue);
        for (const MenuItem &item : block.items) {
            if (!items[block.venue].contains(item.name))
                items[block.venue].append(item.name);
        }
    }

    QVariantList out;
    for (const QString &venue : order) {
        const QStringList names = items.value(venue);
        QString text = names.mid(0, 4).join(QStringLiteral(", "));
        if (names.size() > 4)
            text += QStringLiteral(" and %1 more").arg(names.size() - 4);
        out.append(QVariantMap{{QStringLiteral("name"), venue}, {QStringLiteral("text"), text}});
    }
    return out;
}

QString DiningViewModel::mealsPeriodText() const
{
    const QString period = m_plan.periodText();
    return period.isEmpty() ? QStringLiteral("left") : QStringLiteral("left ") + period;
}

QString DiningViewModel::planTitle() const
{
    if (m_plan.period == u"week") {
        if (const auto meals = m_plan.mealsPerPeriod())
            return QStringLiteral("%1-meal plan").arg(*meals);
    }
    return !m_plan.planName.isEmpty() ? m_plan.planName : QStringLiteral("Meal plan");
}

int DiningViewModel::scansPerPeriod() const
{
    if (m_plan.period == u"week")
        return 1;
    if (m_plan.period == u"term")
        return 5;
    return 0;
}

QString DiningViewModel::exchangeText() const
{
    const QDate today = now().date();
    // Venues grouped by when their exchange window closes today.
    QMap<int, QStringList> byClose;
    for (const hours::Venue &venue : hours::venues()) {
        const auto &window = venue.exchange[static_cast<int>(hours::diningDayOf(today))];
        if (window)
            byClose[window->close].append(venue.name.section(QStringLiteral(" /"), 0, 0));
    }
    QStringList parts;
    for (auto it = byClose.cbegin(); it != byClose.cend(); ++it)
        parts.append(it.value().join(QStringLiteral(", ")) + QStringLiteral(" until ")
                     + fmt::clockShort(QTime(0, 0).addSecs((it.key() % 1440) * 60)));
    return parts.join(QStringLiteral("; "));
}

QString DiningViewModel::flexPerWeekText() const
{
    const auto pace = flexPace(m_plan, now());
    return pace ? MealPlan::money(pace->weekly) + QStringLiteral(" a week") : QString();
}

QString DiningViewModel::paceEndText() const
{
    const auto pace = flexPace(m_plan, now());
    return pace ? fmt::monthDay(pace->termEnds) : QString();
}

QString DiningViewModel::spentThisWeek() const
{
    const auto pace = flexPace(m_plan, now());
    return pace ? MealPlan::money(pace->spentThisWeek) : QString();
}

bool DiningViewModel::overPace() const
{
    const auto pace = flexPace(m_plan, now());
    return pace && pace->spentThisWeek > pace->weekly + 0.005;
}

QString DiningViewModel::paceDeltaText() const
{
    const auto pace = flexPace(m_plan, now());
    if (!pace)
        return {};
    const double delta = pace->spentThisWeek - pace->weekly;
    return MealPlan::money(std::fabs(delta)) + (delta > 0.005 ? QStringLiteral(" over pace")
                                                              : QStringLiteral(" under pace"));
}

double DiningViewModel::paceSpentFraction() const
{
    const auto pace = flexPace(m_plan, now());
    if (!pace)
        return 0.0;
    const double scale = std::max(pace->spentThisWeek, pace->weekly);
    return scale > 0.0 ? pace->spentThisWeek / scale : 0.0;
}

double DiningViewModel::paceBudgetFraction() const
{
    const auto pace = flexPace(m_plan, now());
    if (!pace)
        return 0.0;
    const double scale = std::max(pace->spentThisWeek, pace->weekly);
    return scale > 0.0 ? pace->weekly / scale : 0.0;
}

void DiningViewModel::setActivityFilter(int filter)
{
    filter = std::clamp(filter, 0, 2);
    if (filter != m_activityFilter) {
        m_activityFilter = filter;
        rebuildActivity();
    }
}

QString DiningViewModel::activitySummary() const
{
    const QList<MealTransaction> shown = shownActivity();
    if (shown.isEmpty())
        return {};

    double spent = 0.0;
    double added = 0.0;
    qsizetype purchases = 0;
    qsizetype meals = 0;
    for (const MealTransaction &t : shown) {
        if (t.amount && !t.isDeposit) {
            spent += *t.amount;
            ++purchases;
        }
        if (t.amount && t.isDeposit)
            added += *t.amount;
        if (!t.isFlex())
            ++meals;
    }

    QStringList parts;
    QDateTime oldest;
    for (const MealTransaction &t : m_plan.transactions) {
        if (t.at.isValid() && (!oldest.isValid() || t.at < oldest))
            oldest = t.at;
    }
    if (oldest.isValid())
        parts.append(QStringLiteral("Since ") + fmt::monthDay(oldest.date()));
    if (m_activityFilter == 2) {
        parts.append(count(purchases, QStringLiteral("purchase")));
        parts.append(MealPlan::money(spent));
    } else if (m_activityFilter == 1) {
        parts.append(count(meals, QStringLiteral("meal")));
    } else {
        parts.append(count(meals, QStringLiteral("meal")));
        parts.append(MealPlan::money(spent) + QStringLiteral(" flex spent"));
    }
    if (added != 0.0)
        parts.append(QStringLiteral("+") + MealPlan::money(added) + QStringLiteral(" added"));
    return parts.join(QStringLiteral(" · "));
}

QString DiningViewModel::activityEmptyText() const
{
    if (!shownActivity().isEmpty())
        return {};
    if (!m_planStatus.hasData())
        return QStringLiteral("Sign in to see your meal plan activity.");
    if (m_activityFilter == 2 && !m_plan.transactions.isEmpty())
        return QStringLiteral("No flex purchases in your recent activity.");
    if (m_activityFilter == 1 && !m_plan.transactions.isEmpty())
        return QStringLiteral("No meals in your recent activity.");
    return QStringLiteral("No recent activity on this card.");
}

QList<MealTransaction> DiningViewModel::shownActivity() const
{
    if (m_activityFilter == 0)
        return m_plan.transactions;
    QList<MealTransaction> out;
    for (const MealTransaction &t : m_plan.transactions) {
        if (t.isFlex() == (m_activityFilter == 2))
            out.append(t);
    }
    return out;
}

void DiningViewModel::rebuildActivity()
{
    m_activity.replace(shownActivity(), now().date());
    emit changed();
}

QString DiningViewModel::nextMealLabel() const
{
    return m_nextMealBlock ? m_nextMealBlock->heading() : QString();
}

QString DiningViewModel::nextMealWhen() const
{
    if (!m_nextMealOn.isValid())
        return {};
    const QDate today = now().date();
    if (m_nextMealOn == today)
        return QStringLiteral("Up next");
    if (m_nextMealOn == today.addDays(1))
        return QStringLiteral("Tomorrow");
    return fmt::dayLong(m_nextMealOn);
}

QString DiningViewModel::nextMealHours() const
{
    if (!m_nextMealBlock || !m_nextMealOn.isValid())
        return {};
    const std::optional<ServingHours> hours = servingHours(m_nextMealOn, m_nextMealBlock->slot);
    return hours ? formatHours(hours->first, hours->second) : QString();
}

QString DiningViewModel::nextMealVenue() const
{
    return m_nextMealBlock ? m_nextMealBlock->venue : HOME_COOKING;
}

QList<DayMenu> DiningViewModel::menus() const
{
    QList<DayMenu> out = m_byDate.values();
    std::sort(out.begin(), out.end(), [](const DayMenu &a, const DayMenu &b) { return a.on < b.on; });
    return out;
}

void DiningViewModel::refreshPlan()
{
    if (m_planBusy)
        return;
    m_planBusy = true;
    m_planStatus.begin();
    emit changed();

    const int generation = m_generation;
    runInBackground(
        this,
        [provider = m_mealsProvider] {
            QElapsedTimer timer;
            timer.start();
            QJsonObject payload = provider->fetchPayload();
            qCInfo(lcDining) << "meal plan: fetched in" << timer.elapsed() << "ms";
            return payload;
        },
        [this, generation](const QJsonObject &payload) {
            if (generation == m_generation)
                onPlanPayloadLoaded(payload);
        },
        [this, generation](std::exception_ptr error) {
            if (generation == m_generation)
                onPlanFailed(error);
        });
}

void DiningViewModel::onPlanPayloadLoaded(const QJsonObject &payload)
{
    MealPlan plan;
    try {
        plan = parseBalance(payload);
    } catch (...) {
        onPlanFailed(std::current_exception());
        return;
    }
    if (persisting()) {
        m_storage->cache.save(cachekey::MEALS, payload, now());
        const MealsTarget target = m_mealsProvider->target();
        m_storage->session.update([&](SessionState &state) {
            state.mealsPersonId = target.personId;
            state.mealsCard = target.card;
            state.lastSuccess = QDateTime::currentMSecsSinceEpoch() / 1000.0;
        });
    }
    onPlanLoaded(plan);
}

void DiningViewModel::onPlanLoaded(const MealPlan &plan)
{
    m_plan = plan;
    m_planBusy = false;
    m_planStatus.succeeded(now());
    qCInfo(lcDining).noquote() << QStringLiteral("meal plan: %1 meals, dining=%2, flex=%3, %4 transactions")
                                      .arg(m_plan.mealsRemaining ? QString::number(*m_plan.mealsRemaining)
                                                                 : QStringLiteral("None"),
                                           MealPlan::money(m_plan.diningDollars),
                                           MealPlan::money(m_plan.flexDollars))
                                      .arg(m_plan.transactions.size());
    rebuildActivity();
}

void DiningViewModel::onPlanFailed(std::exception_ptr error)
{
    m_planBusy = false;
    QString message;
    try {
        std::rethrow_exception(error);
    } catch (const SessionExpired &) {
        m_planStatus.stopped();
        emit changed();
        emit sessionExpired();
        return;
    } catch (const ParseError &e) {
        // The server's own "could not reach the meal plan system" is worth
        // passing on as it is; anything else means the page changed.
        message = e.message().startsWith(QStringLiteral("Self-Service could not"))
            ? e.message()
            : QStringLiteral("Self-Service's meal plan page didn't look the way this app expects.");
    } catch (const TransportError &e) {
        message = QStringLiteral("Couldn't reach Self-Service: ") + e.message();
    } catch (...) {
        message = QStringLiteral("Unexpected error: ") + describe(error);
    }
    m_planStatus.failed(message, isOffline(error));
    qCWarning(lcDining).noquote() << "meal plan unavailable:" << describe(error);
    emit changed();
}

void DiningViewModel::refreshAll()
{
    refresh();
    refreshPlan();
}

void DiningViewModel::refresh()
{
    if (m_busy)
        return;

    m_busy = true;
    m_error.clear();
    m_menuStatus.begin();
    emit changed();

    const int generation = m_generation;
    fetchMenus(
        DiningProvider(m_transport, m_days),
        [this, generation](const QList<DayMenu> &menus, const QJsonValue &payload) {
            if (generation != m_generation)
                return;
            if (persisting() && !payload.isUndefined() && !payload.isNull())
                m_storage->cache.save(cachekey::MENUS, payload, now());
            onLoaded(menus);
        },
        [this, generation](std::exception_ptr error) {
            if (generation == m_generation)
                onFailed(error);
        });
}

void DiningViewModel::nextDay()
{
    moveTo(m_offset + 1);
}

void DiningViewModel::previousDay()
{
    moveTo(m_offset - 1, true);
}

void DiningViewModel::goToToday()
{
    moveTo(0);
}

void DiningViewModel::selectDay(int offset)
{
    moveTo(offset, offset < m_offset);
}

void DiningViewModel::selectMeal(const QString &slot)
{
    if (!SLOT_ORDER.contains(slot))
        return;
    m_mealPinned = true;
    if (slot == m_meal)
        return;
    m_meal = slot;
    rebuildStations();
    emit changed();
}

void DiningViewModel::toggleAvoid(const QString &allergen)
{
    const QString key = allergen.toLower();
    if (m_avoid.contains(key))
        m_avoid.removeAll(key);
    else
        m_avoid.append(key);
    saveAvoid();
}

void DiningViewModel::clearAvoid()
{
    if (m_avoid.isEmpty())
        return;
    m_avoid.clear();
    saveAvoid();
}

void DiningViewModel::saveAvoid()
{
    if (m_settings)
        m_settings->setAvoid(m_avoid);
    rebuildStations();
    emit changed();
}

void DiningViewModel::moveTo(int offset, bool backward)
{
    if (offset == m_offset)
        return;
    m_offset = offset;
    m_dayError.clear();
    rebuild(backward);
}

void DiningViewModel::rebuildStations()
{
    if (!m_mealPinned)
        m_meal = autoMeal();
    QList<MenuBlock> blocks;
    if (const auto day = m_byDate.constFind(selectedDate()); day != m_byDate.cend()) {
        for (const MenuBlock &block : day->blocks) {
            if (block.slot.toCaseFolded() == m_meal)
                blocks.append(block);
        }
    }
    m_hidden = m_stations.replace(blocks, avoidSet());
}

void DiningViewModel::rebuild(bool backward)
{
    rebuildStations();
    ensureSelectedLoaded(backward);
    rebuildNextMeal();
    emit changed();
}

void DiningViewModel::ensureSelectedLoaded(bool backward)
{
    const QDate target = selectedDate();
    if (m_byDate.contains(target) || m_pending.contains(target) || !fetchMenus)
        return;

    const QDate today = now().date();
    if (m_busy && today <= target && target < today.addDays(m_days))
        return; // the refresh in flight covers it

    const QDate start = backward ? target.addDays(-(WINDOW_DAYS - 1)) : target;
    QSet<QDate> window;
    for (int i = 0; i < WINDOW_DAYS; ++i) {
        const QDate d = start.addDays(i);
        if (!m_byDate.contains(d))
            window.insert(d);
    }
    m_pending.unite(window);
    m_dayError.clear();

    const int generation = m_generation;
    fetchMenus(
        DiningProvider(m_transport, WINDOW_DAYS, start),
        [this, window, generation](const QList<DayMenu> &menus, const QJsonValue &) {
            if (generation == m_generation)
                onWindowLoaded(window, menus);
        },
        [this, window, generation](std::exception_ptr error) {
            if (generation == m_generation)
                onWindowFailed(window, error);
        });
}

void DiningViewModel::store(const QSet<QDate> &dates, const QList<DayMenu> &menus)
{
    for (const DayMenu &day : menus)
        m_byDate.insert(day.on, day);
    for (const QDate &on : dates) {
        if (!m_byDate.contains(on))
            m_byDate.insert(on, DayMenu{on, {}});
    }
}

void DiningViewModel::onWindowLoaded(const QSet<QDate> &window, const QList<DayMenu> &menus)
{
    m_pending.subtract(window);
    store(window, menus);
    qCInfo(lcDining).noquote() << "dining:" << window.size() << "days paged in from"
                               << firstOf(window).toString(Qt::ISODate);
    rebuild();
}

void DiningViewModel::onWindowFailed(const QSet<QDate> &window, std::exception_ptr error)
{
    // No rebuild here: it would retry at once, and a retry loop against a
    // service that is down helps nobody. Moving away and back retries.
    m_pending.subtract(window);
    if (window.contains(selectedDate()))
        m_dayError = menuErrorText(error);
    qCCritical(lcDining).noquote() << "dining window from" << firstOf(window).toString(Qt::ISODate)
                                   << "failed:" << describe(error);
    emit changed();
}

void DiningViewModel::rebuildNextMeal()
{
    applyNextMeal(nextMealBlock(m_menus, now()));
}

void DiningViewModel::tick()
{
    // The next sitting's list is reset only when it moved: this runs twice a
    // minute, and rebuilding the card's rows that often would be noticed.
    const auto found = nextMealBlock(m_menus, now());
    const bool same = found ? (m_nextMealBlock && found->first == m_nextMealOn && found->second == *m_nextMealBlock)
                            : !m_nextMealBlock.has_value();
    if (!same)
        applyNextMeal(found);
    const bool mealMoved = !m_mealPinned && autoMeal() != m_meal;
    if (mealMoved)
        rebuildStations();
    const bool newDay = m_tickDay != now().date();
    m_tickDay = now().date();
    m_menuStatus.tick();
    m_planStatus.tick();
    // The lists only change when the sitting or the day does.
    if (!same || mealMoved || newDay)
        emit changed();
    else
        emit clockChanged();
}

void DiningViewModel::applyNextMeal(const std::optional<std::pair<QDate, MenuBlock>> &found)
{
    if (!found) {
        m_nextMealOn = QDate();
        m_nextMealBlock.reset();
        m_nextMeal.replaceItems({});
        return;
    }

    m_nextMealOn = found->first;
    m_nextMealBlock = found->second;
    m_nextMeal.replaceItems(m_nextMealBlock->items);
    qCDebug(lcDining).noquote() << "next meal:" << m_nextMealBlock->heading() << "on"
                                << m_nextMealOn.toString(Qt::ISODate) << "(" << m_nextMealBlock->items.size()
                                << "items)";
}

void DiningViewModel::applyMenus(const QList<DayMenu> &menus)
{
    m_menus = menus;
    m_byDate.clear();
    QSet<QDate> dates;
    for (const DayMenu &day : menus)
        dates.insert(day.on);
    store(dates, menus);
}

void DiningViewModel::onLoaded(const QList<DayMenu> &menus)
{
    m_busy = false;
    m_error.clear();
    // A refresh starts over, so a day moved to earlier is fetched again when
    // next shown rather than served stale forever.
    applyMenus(menus);
    const QDate today = now().date();
    QSet<QDate> requested;
    for (int i = 0; i < m_days; ++i)
        requested.insert(today.addDays(i));
    store(requested, menus);
    m_menuStatus.succeeded(now());
    qCInfo(lcDining) << "dining:" << menus.size() << "days loaded";
    rebuild();
}

void DiningViewModel::onFailed(std::exception_ptr error)
{
    m_busy = false;
    m_error = menuErrorText(error);
    m_menuStatus.failed(m_error, isOffline(error));
    qCCritical(lcDining).noquote() << "dining refresh failed:" << describe(error);
    emit changed();
}

void DiningViewModel::clearPersonal()
{
    ++m_generation;
    m_planBusy = false;
    m_plan = MealPlan();
    m_planStatus.reset();
    resetProvider({});
    rebuildActivity();
}

void DiningViewModel::setPreview(bool on)
{
    if (on == m_preview)
        return;
    m_preview = on;
    ++m_generation;
    m_busy = false;
    m_planBusy = false;
    m_pending.clear();
    m_byDate.clear();
    m_menus.clear();
    m_plan = MealPlan();
    m_error.clear();
    m_dayError.clear();
    m_offset = 0;
    m_mealPinned = false;
    m_menuStatus.reset();
    m_planStatus.reset();
    m_activity.replace({}, now().date());
    hydrate();
    emit changed();
}

} // namespace mycu
