// Dining: the Home Cooking menu (Summary card and Chucks tab) and meal-plan
// balances and activity (Summary and Dining tabs).

#pragma once

#include "core/models.h"
#include "core/providers/dining.h"
#include "core/transport.h"

#include <QAbstractListModel>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <exception>
#include <functional>
#include <optional>

namespace mycu {

// How many days one Chucks-tab fetch asks for. Paging a day at a time then
// costs one request per week travelled rather than one per tap, and a week of
// menus is about a hundred kilobytes.
inline constexpr int WINDOW_DAYS = 7;

// How often the summary card re-checks the clock, in milliseconds. A sitting
// ends on the minute, so this keeps the switchover within half a minute of it.
inline constexpr int NEXT_MEAL_CHECK_MS = 30000;

// A flat list of headers and dishes, for a QML ListView.
//
// Flat rather than nested on purpose: QML list views want one model, and a
// list of lists cannot be bound without a second model class per level. A row
// is either a meal header (`isHeader`, with that sitting's serving `hours`) or a
// dish, and the delegate picks a look from that. It also means section headers scroll naturally with their items,
// which is what you want on a phone.
class MenuListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        TextRole = Qt::UserRole + 1,
        AllergenRole,
        HeaderRole,
        HoursRole,
    };

    using QAbstractListModel::QAbstractListModel;

    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // `on` picks the serving hours: Chuck's keeps different ones at weekends.
    void replaceFromBlocks(const QList<MenuBlock> &blocks, QDate on);

    // Dishes only, no heading row.
    //
    // The summary screen's card already carries the meal name in its own
    // header, so a "BREAKFAST" row inside the list would say it twice.
    void replaceItems(const QList<MenuItem> &items);

private:
    struct Row
    {
        QString text;
        QString allergens;
        bool isHeader = false;
        QString hours;
    };
    QList<Row> m_rows;
};

// Recent meal-plan activity, flat, with a header row per day.
//
// Flat for the same reason as MenuListModel: one model, one Repeater, and the
// delegate picks a look from `isHeader`.
class ActivityListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        HeaderRole = Qt::UserRole + 1,
        TitleRole,
        DetailRole,
        AmountRole,
        FlexRole,
        DepositRole,
    };

    using QAbstractListModel::QAbstractListModel;

    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Rows for `transactions` (already newest first), grouped by day.
    void replace(const QList<MealTransaction> &transactions, QDate today);

private:
    struct Row
    {
        bool isHeader = false;
        QString title, detail, amount;
        bool isFlex = false;
        bool isDeposit = false;
    };
    QList<Row> m_rows;
};

// State and actions for the dining screens.
//
// refresh() fetches the week from today, which is what the summary card's next
// sitting is picked from. The Chucks tab pages through any date at all: days
// already fetched are served from a cache, and a date that is not there is
// fetched with the week around it (WINDOW_DAYS) — the menu does not change
// minute to minute, and a request per tap would be rude to a service that is
// reformatting someone else's data.
//
// Exposed to QML as the context property `dining`.
class DiningViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QObject *items READ items CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString venue READ venue NOTIFY changed)
    Q_PROPERTY(QString dateText READ dateText NOTIFY changed)
    Q_PROPERTY(QString dateDetail READ dateDetail NOTIFY changed)
    Q_PROPERTY(int dayOffset READ dayOffset NOTIFY changed)
    Q_PROPERTY(bool isToday READ isToday NOTIFY changed)
    Q_PROPERTY(bool dayLoading READ dayLoading NOTIFY changed)
    Q_PROPERTY(QString dayEmptyText READ dayEmptyText NOTIFY changed)

    // ---- Meal plan
    // `-1` / "" are the "not reported" sentinels: QML has no null, and a
    // confident 0 or "$0.00" for an unknown balance would misstate money.
    Q_PROPERTY(int mealsRemaining READ mealsRemaining NOTIFY changed)
    // The plan's own dollars ("Flex Dollars" on the page) — these **expire at
    // the end of term**.
    Q_PROPERTY(QString diningDollars READ diningDollars NOTIFY changed)
    // Voluntary Flex Dollars — purchased separately, these **do not expire**.
    Q_PROPERTY(QString flexDollars READ flexDollars NOTIFY changed)
    Q_PROPERTY(bool hasPlan READ hasPlan NOTIFY changed)
    Q_PROPERTY(QString mealsPeriodText READ mealsPeriodText NOTIFY changed)
    Q_PROPERTY(QString planDescription READ planDescription NOTIFY changed)

    // ---- Recent activity (the Dining tab)
    Q_PROPERTY(QObject *activity READ activity CONSTANT)
    Q_PROPERTY(bool flexOnly READ flexOnly NOTIFY changed)
    Q_PROPERTY(QString activitySummary READ activitySummary NOTIFY changed)
    Q_PROPERTY(QString activityEmptyText READ activityEmptyText NOTIFY changed)

    // ---- The next sitting
    // Public data, so this fills in before sign-in and stays filled in after a
    // sign-out — unlike the meal plan.
    Q_PROPERTY(QObject *nextMealItems READ nextMealItems CONSTANT)
    Q_PROPERTY(bool hasNextMeal READ hasNextMeal NOTIFY changed)
    Q_PROPERTY(QString nextMealLabel READ nextMealLabel NOTIFY changed)
    Q_PROPERTY(QString nextMealWhen READ nextMealWhen NOTIFY changed)
    Q_PROPERTY(QString nextMealHours READ nextMealHours NOTIFY changed)
    Q_PROPERTY(QString nextMealVenue READ nextMealVenue NOTIFY changed)

public:
    using MenusDone = std::function<void(const QList<DayMenu> &)>;
    using MenusFailed = std::function<void(std::exception_ptr)>;
    // How a menu fetch is started. Runs `provider.fetch()` on a worker thread
    // in the app; the tests swap it for one that records the request and
    // answers it by hand, which is what makes the paging logic testable
    // without threads.
    using MenuFetcher = std::function<void(DiningProvider, MenusDone, MenusFailed)>;

    explicit DiningViewModel(TransportPtr transport, int days = DEFAULT_DAYS, QObject *parent = nullptr);

    QObject *items() { return &m_model; }
    bool busy() const { return m_busy; }
    bool loaded() const { return m_loaded; }
    QString error() const { return m_error; }
    QString venue() const { return HOME_COOKING; }

    // "Today", "Tomorrow", "Yesterday", or a short date.
    QString dateText() const;
    // "Tuesday, September 22", with the year only when it is not this one.
    QString dateDetail() const;
    int dayOffset() const { return m_offset; }
    bool isToday() const { return m_offset == 0; }
    // Whether the selected day is on its way — by window fetch or refresh.
    bool dayLoading() const;

    // Why the selected day has no menu, or "" when it has one.
    //
    // There is no "can't go further" here: which days are posted is not known
    // until they are asked for, and the gaps (breaks, holidays) are in the
    // middle as well as at the ends. So every day can be paged to, and one
    // with nothing on it says so.
    QString dayEmptyText() const;

    int mealsRemaining() const { return m_plan.mealsRemaining.value_or(-1); }
    QString diningDollars() const { return MealPlan::money(m_plan.diningDollars); }
    QString flexDollars() const { return MealPlan::money(m_plan.flexDollars); }
    bool hasPlan() const { return m_plan.hasAny(); }

    // "left this week" / "left this term", or just "left".
    //
    // The qualifier is only there when the page actually said which cycle the
    // count runs on — see MealPlan::period.
    QString mealsPeriodText() const;

    // "21 Meals per week" / "Block 120" / "Weekly meal plan" / "".
    QString planDescription() const { return m_plan.planDescription(); }

    QObject *activity() { return &m_activity; }
    bool flexOnly() const { return m_flexOnly; }

    // "Since Sep 14 · 20 meals · $9.74 flex spent", or "" with nothing to sum.
    //
    // Meals are the swipes (board meals and exchanges). With the flex filter
    // on, the count is of purchases instead.
    QString activitySummary() const;

    // Why the list is empty, or "" when it is not.
    QString activityEmptyText() const;

    QObject *nextMealItems() { return &m_nextMeal; }
    bool hasNextMeal() const { return m_nextMealBlock.has_value(); }
    // "Breakfast" / "Lunch" / "Dinner" — the sitting's own heading.
    QString nextMealLabel() const;

    // "Up next" when it is today, otherwise which day it is.
    //
    // After the last sitting of the day the next meal is tomorrow's breakfast,
    // and calling that "up next" without saying so would have people turning
    // up to a closed dining hall.
    QString nextMealWhen() const;

    // "10:30am–2:30pm" — that day's serving window, or "".
    //
    // Read off the sitting's own date, so tomorrow's breakfast on a Saturday
    // says "8am–9am", not the weekday hours.
    QString nextMealHours() const;
    QString nextMealVenue() const;

    // Which sitting is next is entirely a function of the clock, so the clock
    // is a seam. Without it the test for "after dinner, show tomorrow's
    // breakfast" could only be run after dinner.
    std::function<QDateTime()> now = [] { return QDateTime::currentDateTime(); };

    MenuFetcher fetchMenus;

public slots:
    // A slot, not a writable property: ToggleSwitch binds `on` and reports
    // `clicked`, and the switch moves when this does.
    void setFlexOnly(bool on);

    // Load the meal-plan balances. Needs the Cedarville session.
    void refreshPlan();

    // Everything on this screen: the menu *and* the meal-plan balances.
    //
    // These come from two different services with different auth, and before
    // this existed only the menu was reachable from the UI — the balances
    // loaded once at sign-in and then stayed on screen unchanged for the rest
    // of the run, which is a bad way to show a number that goes down every
    // time you eat.
    void refreshAll();

    void refresh();
    void nextDay();
    void previousDay();
    void goToToday();

signals:
    void changed();

private:
    friend class TestViewModels;

    QDate selectedDate() const;
    void moveTo(int offset, bool backward = false);
    void rebuild(bool backward = false);
    // Fetch the week around the selected day, if nothing has it yet.
    //
    // The window extends in the direction of travel — back from the target
    // when paging backwards — so the next six taps that way are free.
    void ensureSelectedLoaded(bool backward);
    // Cache `menus`, and an empty day for any of `dates` it lacks.
    //
    // The server answers every date it is asked for, but if it ever skipped
    // one, leaving the gap uncached would have rebuild() ask for it again on
    // every rebuild.
    void store(const QSet<QDate> &dates, const QList<DayMenu> &menus);
    void onWindowLoaded(const QSet<QDate> &window, const QList<DayMenu> &menus);
    void onWindowFailed(const QSet<QDate> &window, std::exception_ptr error);
    // Re-pick the sitting the summary screen shows.
    //
    // Driven off the clock, so it is recomputed on every load and every
    // refresh rather than cached: an app left open over lunch should be
    // showing dinner by the time you look at it again.
    void rebuildNextMeal();
    // Re-pick the next sitting off the clock, and speak up only if it moved.
    //
    // Runs every NEXT_MEAL_CHECK_MS, so it must not reset the model when
    // nothing changed — that would rebuild the card's list twice a minute for
    // no reason.
    void tick();
    void applyNextMeal(const std::optional<std::pair<QDate, MenuBlock>> &found);
    void onLoaded(const QList<DayMenu> &menus);
    void onFailed(std::exception_ptr error);
    void onPlanLoaded(const MealPlan &plan);
    QList<MealTransaction> shownActivity() const;
    void rebuildActivity();

    TransportPtr m_transport;
    int m_days;
    MenuListModel m_model;
    QList<DayMenu> m_menus;
    int m_offset = 0;

    // Every day fetched so far, by date — what the Chucks tab reads. Kept
    // apart from m_menus, which stays the week from today because the summary
    // card needs it wherever the tab has paged to.
    QHash<QDate, DayMenu> m_byDate;
    // Dates a window fetch has been sent for and not answered, so tapping back
    // three times in a row sends one request rather than three.
    QSet<QDate> m_pending;
    QString m_dayError;

    // The one sitting the summary screen shows, picked off the clock. A second
    // model rather than a filtered view of the first: the Chucks tab pages
    // through days independently, and the summary must keep showing the next
    // meal while it does.
    MenuListModel m_nextMeal;
    QDate m_nextMealOn;
    std::optional<MenuBlock> m_nextMealBlock;

    // Loads are not the only thing that moves the next sitting on — the clock
    // does too. An app left open through 9:30 should flip from breakfast to
    // lunch without being refreshed. Repeating rather than aimed at the next
    // cutoff, so a phone waking from sleep catches up on the first tick
    // instead of trusting a deadline it slept through.
    QTimer m_nextMealTimer;

    bool m_busy = false;
    QString m_error;
    bool m_loaded = false;

    // The meal plan is a *different* source from the menus: a Self-Service
    // page behind the Cedarville sign-in, versus the public menu API. It sits
    // on this screen because that is where a reader expects it, and it loads
    // independently so a failure in one never blanks the other.
    MealPlan m_plan;
    bool m_planLoaded = false;

    // The Dining tab's history, and its one filter. Filtered here rather than
    // in QML so the list, the summary line and the empty text all agree about
    // what is being shown.
    ActivityListModel m_activity;
    bool m_flexOnly = false;
};

} // namespace mycu
