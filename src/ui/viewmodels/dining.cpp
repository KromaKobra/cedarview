#include "dining.h"

#include "core/errors.h"
#include "core/log.h"
#include "core/providers/meals.h"
#include "format.h"
#include "ui/tasks.h"

#include <algorithm>

namespace mycu {

// ---------------------------------------------------------------------------
// MenuListModel
// ---------------------------------------------------------------------------

QHash<int, QByteArray> MenuListModel::roleNames() const
{
    return {{TextRole, "text"}, {AllergenRole, "allergens"}, {HeaderRole, "isHeader"}, {HoursRole, "hours"}};
}

int MenuListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant MenuListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case TextRole:
        return row.text;
    case AllergenRole:
        return row.allergens;
    case HeaderRole:
        return row.isHeader;
    case HoursRole:
        return row.hours;
    default:
        return {};
    }
}

void MenuListModel::replaceFromBlocks(const QList<MenuBlock> &blocks, QDate on)
{
    beginResetModel();
    m_rows.clear();
    for (const MenuBlock &block : blocks) {
        // Empty for all-day stations, which have no window.
        const std::optional<ServingHours> hours = servingHours(on, block.slot);
        m_rows.append({block.heading(), QString(), true,
                       hours ? formatHours(hours->first, hours->second) : QString()});
        for (const MenuItem &item : block.items)
            m_rows.append({item.name, item.allergenText(), false});
    }
    endResetModel();
}

void MenuListModel::replaceItems(const QList<MenuItem> &items)
{
    beginResetModel();
    m_rows.clear();
    for (const MenuItem &item : items)
        m_rows.append({item.name, item.allergenText(), false});
    endResetModel();
}

// ---------------------------------------------------------------------------
// ActivityListModel
// ---------------------------------------------------------------------------

namespace {

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

} // namespace

QHash<int, QByteArray> ActivityListModel::roleNames() const
{
    return {
        {HeaderRole, "isHeader"}, {TitleRole, "title"}, {DetailRole, "detail"},
        {AmountRole, "amount"},   {FlexRole, "isFlex"}, {DepositRole, "isDeposit"},
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
    default:
        return {};
    }
}

void ActivityListModel::replace(const QList<MealTransaction> &transactions, QDate today)
{
    beginResetModel();
    m_rows.clear();
    bool first = true;
    QDate lastDay;
    for (const MealTransaction &t : transactions) {
        const QDate day = t.at.isValid() ? t.at.date() : QDate();
        if (first || day != lastDay) {
            Row header;
            header.isHeader = true;
            header.title = dayLabel(day, today);
            m_rows.append(header);
            lastDay = day;
            first = false;
        }
        QStringList parts;
        if (!t.mealPeriod.isEmpty())
            parts.append(t.mealPeriod);
        if (t.at.isValid())
            parts.append(fmt::clock(t.at.time()));
        m_rows.append({false, t.activity, parts.join(QStringLiteral(" · ")), t.amountText(), t.isFlex(),
                       t.isDeposit});
    }
    endResetModel();
}

// ---------------------------------------------------------------------------
// DiningViewModel
// ---------------------------------------------------------------------------

DiningViewModel::DiningViewModel(TransportPtr transport, int days, QObject *parent)
    : QObject(parent)
    , m_transport(std::move(transport))
    , m_days(days)
    , m_model(this)
    , m_nextMeal(this)
    , m_activity(this)
{
    fetchMenus = [this](DiningProvider provider, MenusDone done, MenusFailed failed) {
        runInBackground(
            this, [provider]() mutable { return provider.fetch(); },
            [done](const QList<DayMenu> &menus) { done(menus); }, failed);
    };

    m_nextMealTimer.setInterval(NEXT_MEAL_CHECK_MS);
    connect(&m_nextMealTimer, &QTimer::timeout, this, &DiningViewModel::tick);
    m_nextMealTimer.start();
}

QDate DiningViewModel::selectedDate() const
{
    return QDate::currentDate().addDays(m_offset);
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
    if (day.year() != QDate::currentDate().year())
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
    if (m_model.rowCount())
        return {};
    if (dayLoading())
        return QStringLiteral("Loading the menu…");
    if (!m_dayError.isEmpty())
        return m_dayError;
    if (!m_byDate.contains(selectedDate()) && !m_error.isEmpty())
        return m_error;
    return QStringLiteral("Nothing posted for %1 on this day.").arg(HOME_COOKING);
}

QString DiningViewModel::mealsPeriodText() const
{
    const QString period = m_plan.periodText();
    return period.isEmpty() ? QStringLiteral("left") : QStringLiteral("left ") + period;
}

void DiningViewModel::setFlexOnly(bool on)
{
    if (on != m_flexOnly) {
        m_flexOnly = on;
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
    if (m_flexOnly) {
        parts.append(count(purchases, QStringLiteral("purchase")));
        parts.append(MealPlan::money(spent));
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
    if (!m_planLoaded)
        return QStringLiteral("Sign in to see your meal plan activity.");
    if (m_flexOnly && !m_plan.transactions.isEmpty())
        return QStringLiteral("No flex purchases in your recent activity.");
    return QStringLiteral("No recent activity on this card.");
}

QList<MealTransaction> DiningViewModel::shownActivity() const
{
    if (!m_flexOnly)
        return m_plan.transactions;
    QList<MealTransaction> flex;
    for (const MealTransaction &t : m_plan.transactions) {
        if (t.isFlex())
            flex.append(t);
    }
    return flex;
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
    const QDate today = QDate::currentDate();
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

void DiningViewModel::refreshPlan()
{
    runInBackground(
        this, [provider = MealsProvider(m_transport)]() mutable { return provider.fetch(); },
        [this](const MealPlan &plan) { onPlanLoaded(plan); },
        [](std::exception_ptr error) {
            // Quiet on purpose: the menu is the bulk of this screen and should
            // not be replaced by an error banner because one panel is missing.
            qCWarning(lcDining).noquote() << "meal plan unavailable:" << describe(error);
        });
}

void DiningViewModel::onPlanLoaded(const MealPlan &plan)
{
    m_plan = plan;
    m_planLoaded = true;
    qCInfo(lcDining).noquote() << QStringLiteral("meal plan: %1 meals, dining=%2, flex=%3, %4 transactions")
                                      .arg(m_plan.mealsRemaining ? QString::number(*m_plan.mealsRemaining)
                                                                 : QStringLiteral("None"),
                                           MealPlan::money(m_plan.diningDollars),
                                           MealPlan::money(m_plan.flexDollars))
                                      .arg(m_plan.transactions.size());
    rebuildActivity();
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
    emit changed();

    fetchMenus(
        DiningProvider(m_transport, m_days), [this](const QList<DayMenu> &menus) { onLoaded(menus); },
        [this](std::exception_ptr error) { onFailed(error); });
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

void DiningViewModel::moveTo(int offset, bool backward)
{
    if (offset == m_offset)
        return;
    m_offset = offset;
    m_dayError.clear();
    rebuild(backward);
}

void DiningViewModel::rebuild(bool backward)
{
    const auto day = m_byDate.constFind(selectedDate());
    m_model.replaceFromBlocks(day != m_byDate.cend() ? day->forVenue(HOME_COOKING) : QList<MenuBlock>(),
                              selectedDate());
    ensureSelectedLoaded(backward);
    rebuildNextMeal();
    emit changed();
}

void DiningViewModel::ensureSelectedLoaded(bool backward)
{
    const QDate target = selectedDate();
    if (m_byDate.contains(target) || m_pending.contains(target))
        return;

    const QDate today = QDate::currentDate();
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

    fetchMenus(
        DiningProvider(m_transport, WINDOW_DAYS, start),
        [this, window](const QList<DayMenu> &menus) { onWindowLoaded(window, menus); },
        [this, window](std::exception_ptr error) { onWindowFailed(window, error); });
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
    // service that is down helps nobody. Paging away and back retries.
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
    const auto found = nextMealBlock(m_menus, now());
    const bool same = found ? (m_nextMealBlock && found->first == m_nextMealOn && found->second == *m_nextMealBlock)
                            : !m_nextMealBlock.has_value();
    if (same)
        return;
    applyNextMeal(found);
    emit changed();
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
    qCInfo(lcDining).noquote() << "next meal:" << m_nextMealBlock->heading() << "on"
                               << m_nextMealOn.toString(Qt::ISODate) << "(" << m_nextMealBlock->items.size()
                               << "items)";
}

void DiningViewModel::onLoaded(const QList<DayMenu> &menus)
{
    m_menus = menus;
    m_busy = false;
    m_loaded = true;
    m_error.clear();
    // A refresh starts the cache over, so a day paged to earlier is fetched
    // again when next shown rather than served stale forever.
    const QDate today = QDate::currentDate();
    m_byDate.clear();
    QSet<QDate> requested;
    for (int i = 0; i < m_days; ++i)
        requested.insert(today.addDays(i));
    store(requested, m_menus);
    qCInfo(lcDining) << "dining:" << m_menus.size() << "days loaded";
    rebuild();
}

void DiningViewModel::onFailed(std::exception_ptr error)
{
    m_busy = false;
    m_error = menuErrorText(error);
    qCCritical(lcDining).noquote() << "dining refresh failed:" << describe(error);
    emit changed();
}

} // namespace mycu
