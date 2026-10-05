// Dining: the menu (Today's meals, the Menu section) and meal-plan balances
// and activity (Today's tiles, the Plan section).

#pragma once

#include "core/models.h"
#include "core/providers/dining.h"
#include "core/providers/meals.h"
#include "core/transport.h"
#include "sourcestatus.h"
#include "storage.h"

#include <QAbstractListModel>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVariantList>

#include <cmath>
#include <exception>
#include <functional>
#include <memory>
#include <optional>

namespace mycu {

class SettingsController;

// How many days one menu fetch for a day outside the refresh window asks for.
// Paging a day at a time then costs one request per week travelled rather than
// one per tap, and a week of menus is about a hundred kilobytes.
inline constexpr int WINDOW_DAYS = 7;

// How often the next sitting is re-checked against the clock, in milliseconds.
// A sitting ends on the minute, so this keeps the switchover within half a
// minute of it.
inline constexpr int NEXT_MEAL_CHECK_MS = 30000;

// How long each dining source counts as fresh, in seconds.
inline constexpr qint64 MENU_FRESH_SECS = 30 * 60;
inline constexpr qint64 PLAN_FRESH_SECS = 5 * 60;

// The three stations the Menu section is about, in the order it shows them.
inline const QString GARDEN_BITES = QStringLiteral("Garden Bites");
inline const QString ALLERGEN_AWARE = QStringLiteral("Allergen Aware");
inline const QStringList FEATURED_STATIONS = {HOME_COOKING, GARDEN_BITES, ALLERGEN_AWARE};

// The allergens the feed labels, as the Avoid chips offer them. The keys are
// the feed's own `alt` text.
inline const QStringList AVOIDABLE = {QStringLiteral("gluten"), QStringLiteral("dairy"), QStringLiteral("egg"),
                                      QStringLiteral("soy"), QStringLiteral("tree nut")};

// Dishes only, for one sitting: what the Today screen's meal card lists.
class MenuListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        TextRole = Qt::UserRole + 1,
        AllergenRole,
        NewRole,
    };

    using QAbstractListModel::QAbstractListModel;

    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void replaceItems(const QList<MenuItem> &items);

private:
    QList<MenuItem> m_items;
};

// The Menu section's stations for one day and one sitting, flat: a header row
// per station, then its dishes, then (Garden Bites) one row gathering the
// toppings, then a note if the Avoid filter hid everything. Flat for the
// reason every list model here is: one Repeater, and the delegate picks a look
// from `rowType`.
class StationListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        RowTypeRole = Qt::UserRole + 1, // "header" | "item" | "extras" | "note"
        StationRole,
        StationKindRole, // "home" | "garden" | "aware"
        TextRole,
        AllergenRole,
        NewRole,
        HiddenTextRole,
        LastRole,
    };

    using QAbstractListModel::QAbstractListModel;

    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // The featured stations among `blocks` (one sitting's), with any dish
    // carrying an allergen in `avoid` left out. Returns how many were.
    int replace(const QList<MenuBlock> &blocks, const QSet<QString> &avoid);

private:
    struct Row
    {
        QString rowType, station, stationKind, text, allergens, hiddenText;
        bool isNew = false;
        bool isLast = false;
    };
    QList<Row> m_rows;
};

// Recent meal-plan activity, flat, with a header row per day.
//
// Flat for the same reason as StationListModel: one model, one Repeater, and
// the delegate picks a look from `isHeader`. A header's `detail` sums its day
// ("3 meals", "1 meal · $3.74").
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
        KindRole, // "meal" | "exchange" | "flex" | "deposit"
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
        QString kind;
    };
    QList<Row> m_rows;
};

// Where flex spending stands against what would make the balance last the
// term. See flexPace().
struct FlexPace
{
    double weekly = 0.0;     // what can be spent a week
    double spentThisWeek = 0.0;
    QDate termEnds;
};

// The weekly flex budget: the balance as it stood on Monday (now, plus what
// was spent since), spread over the weeks from this Monday to the last day of
// term. Nothing outside a term or without a balance.
std::optional<FlexPace> flexPace(const MealPlan &plan, const QDateTime &now);

// State and actions for the dining screens.
//
// refresh() fetches the week from today, which is what Today's meals and the
// Menu section's first days come from. The Menu section can be moved to any
// date at all: days already fetched are served from memory, and a date that is
// not there is fetched with the week around it (WINDOW_DAYS) — the menu does
// not change minute to minute, and a request per tap would be rude to a
// service that is reformatting someone else's data.
//
// Cache-first, like ChapelViewModel: the constructor reads back the last menu
// window and meal plan saved.
//
// Exposed to QML as the context property `dining`.
class DiningViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QObject *menuStatus READ menuStatus CONSTANT)
    Q_PROPERTY(QObject *planStatus READ planStatus CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString venue READ venue NOTIFY changed)

    // ---- The Menu section
    Q_PROPERTY(QObject *stations READ stations CONSTANT)
    Q_PROPERTY(QString dateText READ dateText NOTIFY changed)
    Q_PROPERTY(QString dateDetail READ dateDetail NOTIFY changed)
    Q_PROPERTY(int dayOffset READ dayOffset NOTIFY changed)
    Q_PROPERTY(bool isToday READ isToday NOTIFY changed)
    Q_PROPERTY(bool dayLoading READ dayLoading NOTIFY changed)
    Q_PROPERTY(QString dayEmptyText READ dayEmptyText NOTIFY changed)
    // A week of days around today: [{offset, dow, num, isToday, selected,
    // available}]. `available` is false only for a day known to have nothing.
    Q_PROPERTY(QVariantList days READ days NOTIFY changed)
    // "breakfast" / "lunch" / "dinner".
    Q_PROPERTY(QString selectedMeal READ selectedMeal NOTIFY changed)
    // [{slot, label, hours, selected}] for the day shown.
    Q_PROPERTY(QVariantList mealTabs READ mealTabs NOTIFY changed)
    // "Opens in 48 min · 10:30 AM – 2:30 PM" / "Serving now · until 2:30 PM" / …
    Q_PROPERTY(QString mealStatusText READ mealStatusText NOTIFY clockChanged)
    // Serving now, or opening within the hour.
    Q_PROPERTY(bool mealStatusLive READ mealStatusLive NOTIFY clockChanged)
    // [{key, label, on}] for the Avoid chips.
    Q_PROPERTY(QVariantList avoidOptions READ avoidOptions NOTIFY changed)
    Q_PROPERTY(QStringList avoid READ avoid NOTIFY changed)
    Q_PROPERTY(int hiddenCount READ hiddenCount NOTIFY changed)
    // "Hiding 3 items with dairy, gluten", or "".
    Q_PROPERTY(QString hiddenText READ hiddenText NOTIFY changed)
    // The stations that serve all day (and, at breakfast, the breakfast bars):
    // [{name, text}].
    Q_PROPERTY(QVariantList allDayStations READ allDayStations NOTIFY changed)

    // ---- Meal plan
    // `-1` / "" are the "not reported" sentinels: QML has no null, and a
    // confident 0 or "$0.00" for an unknown balance would misstate money.
    Q_PROPERTY(int mealsRemaining READ mealsRemaining NOTIFY changed)
    // 21 on "21 Meals", or -1.
    Q_PROPERTY(int mealsPerPeriod READ mealsPerPeriod NOTIFY changed)
    // The plan's own dollars ("Flex Dollars" on the page) — these **expire at
    // the end of term**.
    Q_PROPERTY(QString diningDollars READ diningDollars NOTIFY changed)
    // Voluntary Flex Dollars — purchased separately, these **do not expire**.
    Q_PROPERTY(QString flexDollars READ flexDollars NOTIFY changed)
    // Whether there is any voluntary flex on the account. Most students never
    // buy it, so its figure is hidden rather than shown as "—" or "$0.00".
    Q_PROPERTY(bool hasFlexDollars READ hasFlexDollars NOTIFY changed)
    Q_PROPERTY(bool hasPlan READ hasPlan NOTIFY changed)
    Q_PROPERTY(QString mealsPeriodText READ mealsPeriodText NOTIFY changed)
    Q_PROPERTY(QString planDescription READ planDescription NOTIFY changed)
    // "21-meal plan" / "Block 120".
    Q_PROPERTY(QString planTitle READ planTitle NOTIFY changed)
    Q_PROPERTY(int scansPerPeriod READ scansPerPeriod NOTIFY changed)
    // -1 when not reported.
    Q_PROPERTY(int mealExchanges READ mealExchanges NOTIFY changed)
    // "Chick-fil-A, Panda Express, The Cafe until 8 PM" — where exchanges
    // work today.
    Q_PROPERTY(QString exchangeText READ exchangeText NOTIFY changed)

    // ---- Flex pace (see flexPace())
    Q_PROPERTY(bool hasPace READ hasPace NOTIFY changed)
    // "$8.43 a week".
    Q_PROPERTY(QString flexPerWeekText READ flexPerWeekText NOTIFY changed)
    // "Dec 11".
    Q_PROPERTY(QString paceEndText READ paceEndText NOTIFY changed)
    Q_PROPERTY(QString spentThisWeek READ spentThisWeek NOTIFY changed)
    Q_PROPERTY(bool overPace READ overPace NOTIFY changed)
    // "$1.31 over pace" / "$2.00 under pace".
    Q_PROPERTY(QString paceDeltaText READ paceDeltaText NOTIFY changed)
    // For the pace bar, both relative to whichever of the two is larger.
    Q_PROPERTY(double paceSpentFraction READ paceSpentFraction NOTIFY changed)
    Q_PROPERTY(double paceBudgetFraction READ paceBudgetFraction NOTIFY changed)

    // ---- Recent activity (the Plan section)
    Q_PROPERTY(QObject *activity READ activity CONSTANT)
    // 0 everything, 1 meals only, 2 flex only.
    Q_PROPERTY(int activityFilter READ activityFilter NOTIFY changed)
    Q_PROPERTY(QString activitySummary READ activitySummary NOTIFY changed)
    Q_PROPERTY(QString activityEmptyText READ activityEmptyText NOTIFY changed)

    // ---- The next sitting
    // Public data, so this fills in before sign-in and stays filled in after a
    // sign-out — unlike the meal plan.
    Q_PROPERTY(QObject *nextMealItems READ nextMealItems CONSTANT)
    Q_PROPERTY(bool hasNextMeal READ hasNextMeal NOTIFY changed)
    Q_PROPERTY(QString nextMealLabel READ nextMealLabel NOTIFY changed)
    Q_PROPERTY(QString nextMealWhen READ nextMealWhen NOTIFY clockChanged)
    Q_PROPERTY(QString nextMealHours READ nextMealHours NOTIFY changed)
    Q_PROPERTY(QString nextMealVenue READ nextMealVenue NOTIFY changed)

public:
    // The parsed days, and the payload they were parsed from (which the
    // refresh keeps on the device).
    using MenusDone = std::function<void(const QList<DayMenu> &, const QJsonValue &)>;
    using MenusFailed = std::function<void(std::exception_ptr)>;
    // How a menu fetch is started. Runs `provider.fetchPayload()` and parses it
    // on a worker thread in the app; the tests swap it for one that records the
    // request and answers it by hand, which is what makes the paging logic
    // testable without threads.
    using MenuFetcher = std::function<void(DiningProvider, MenusDone, MenusFailed)>;

    // No `storage`: nothing is read back or saved.
    explicit DiningViewModel(TransportPtr transport, std::optional<Storage> storage = std::nullopt,
                             int days = DEFAULT_DAYS, QObject *parent = nullptr);

    // The Avoid chips are remembered across launches through `settings`.
    void attachSettings(SettingsController *settings);

    SourceStatus *menuStatus() { return &m_menuStatus; }
    SourceStatus *planStatus() { return &m_planStatus; }
    bool busy() const { return m_busy; }
    bool loaded() const { return m_menuStatus.hasData(); }
    QString error() const { return m_error; }
    QString venue() const { return HOME_COOKING; }

    QObject *stations() { return &m_stations; }
    // "Today", "Tomorrow", "Yesterday", or a short date.
    QString dateText() const;
    // "Tuesday, September 22", with the year only when it is not this one.
    QString dateDetail() const;
    int dayOffset() const { return m_offset; }
    bool isToday() const { return m_offset == 0; }
    // Whether the selected day is on its way — by window fetch or refresh.
    bool dayLoading() const;

    // Why the selected sitting shows no stations, or "" when it has some.
    //
    // There is no "can't go further" here: which days are posted is not known
    // until they are asked for, and the gaps (breaks, holidays) are in the
    // middle as well as at the ends. So every day can be moved to, and one
    // with nothing on it says so.
    QString dayEmptyText() const;
    QVariantList days() const;
    QString selectedMeal() const { return m_meal; }
    QVariantList mealTabs() const;
    QString mealStatusText() const;
    bool mealStatusLive() const;
    QVariantList avoidOptions() const;
    QStringList avoid() const;
    int hiddenCount() const { return m_hidden; }
    QString hiddenText() const;
    QVariantList allDayStations() const;

    int mealsRemaining() const { return m_plan.mealsRemaining.value_or(-1); }
    int mealsPerPeriod() const { return m_plan.mealsPerPeriod().value_or(-1); }
    QString diningDollars() const { return MealPlan::money(m_plan.diningDollars); }
    QString flexDollars() const { return MealPlan::money(m_plan.flexDollars); }
    // Anything that would not display as "$0.00".
    bool hasFlexDollars() const { return m_plan.flexDollars && std::fabs(*m_plan.flexDollars) >= 0.005; }
    bool hasPlan() const { return m_plan.hasAny(); }

    // "left this week" / "left this term", or just "left".
    //
    // The qualifier is only there when the page actually said which cycle the
    // count runs on — see MealPlan::period.
    QString mealsPeriodText() const;

    // "21 Meals per week" / "Block 120" / "Weekly meal plan" / "".
    QString planDescription() const { return m_plan.planDescription(); }
    QString planTitle() const;

    // How many times the card can be scanned in one meal period: 1 on a 14- or
    // 21-meal weekly plan, 5 on a block plan, 0 when the plan is not known.
    //
    // Keyed off MealPlan::period, which is only set when the plan's name said
    // which kind it is, so an unrecognised plan gets 0 rather than a guess.
    int scansPerPeriod() const;
    int mealExchanges() const { return m_plan.mealExchanges.value_or(-1); }
    QString exchangeText() const;

    bool hasPace() const { return flexPace(m_plan, now()).has_value(); }
    QString flexPerWeekText() const;
    QString paceEndText() const;
    QString spentThisWeek() const;
    bool overPace() const;
    QString paceDeltaText() const;
    double paceSpentFraction() const;
    double paceBudgetFraction() const;

    QObject *activity() { return &m_activity; }
    int activityFilter() const { return m_activityFilter; }

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

    // Every day in memory, oldest first — what Today and search read.
    QList<DayMenu> menus() const;
    // Whether `day` has been fetched (with or without anything on it).
    bool hasMenuFor(QDate day) const { return m_byDate.contains(day); }
    const MealPlan &plan() const { return m_plan; }

    // Which sitting is next is entirely a function of the clock, so the clock
    // is a seam. Without it the test for "after dinner, show tomorrow's
    // breakfast" could only be run after dinner.
    std::function<QDateTime()> now = [] { return QDateTime::currentDateTime(); };

    MenuFetcher fetchMenus;

public slots:
    // 0 everything, 1 meals, 2 flex.
    void setActivityFilter(int filter);

    // Load the meal-plan balances. Needs the Cedarville session.
    void refreshPlan();

    // Everything on these screens: the menu *and* the meal-plan balances.
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
    // Show the day `offset` days from today.
    void selectDay(int offset);
    // Show one sitting: "breakfast", "lunch" or "dinner".
    void selectMeal(const QString &slot);
    void toggleAvoid(const QString &allergen);
    void clearAvoid();

    // Re-read the clock: the next sitting, the meal status and the pace.
    void tick();

    // Forget the meal plan and its remembered target, in memory. The public
    // menu stays.
    void clearPersonal();
    // Sample data on or off; see ChapelViewModel::setPreview.
    void setPreview(bool on);

signals:
    void changed();
    // Only the clock moved: the sitting's "Opens in 48 min" and friends. Kept
    // apart from changed() so a tick does not make the day strip, the chips
    // and the stations rebuild. Every changed() is also a clockChanged().
    void clockChanged();
    // The meal plan hit an expired session.
    void sessionExpired();

private:
    friend class TestViewModels;

    bool persisting() const { return m_storage.has_value() && !m_preview; }
    void hydrate();
    void resetProvider(const MealsTarget &target);
    QDate selectedDate() const;
    void moveTo(int offset, bool backward = false);
    void rebuild(bool backward = false);
    void rebuildStations();
    // The sitting the clock says to show first: the next one today that has
    // not finished, else dinner.
    QString autoMeal() const;
    // Fetch the week around the selected day, if nothing has it yet.
    //
    // The window extends in the direction of travel — back from the target
    // when paging backwards — so the next six taps that way are free.
    void ensureSelectedLoaded(bool backward);
    // Keep `menus`, and an empty day for any of `dates` it lacks.
    //
    // The server answers every date it is asked for, but if it ever skipped
    // one, leaving the gap unrecorded would have rebuild() ask for it again on
    // every rebuild.
    void store(const QSet<QDate> &dates, const QList<DayMenu> &menus);
    void onWindowLoaded(const QSet<QDate> &window, const QList<DayMenu> &menus);
    void onWindowFailed(const QSet<QDate> &window, std::exception_ptr error);
    // Re-pick the sitting Today shows.
    //
    // Driven off the clock, so it is recomputed on every load and every
    // refresh rather than kept: an app left open over lunch should be showing
    // dinner by the time you look at it again.
    void rebuildNextMeal();
    void applyNextMeal(const std::optional<std::pair<QDate, MenuBlock>> &found);
    void applyMenus(const QList<DayMenu> &menus);
    void onPayloadLoaded(const QJsonValue &payload);
    void onLoaded(const QList<DayMenu> &menus);
    void onFailed(std::exception_ptr error);
    void onPlanPayloadLoaded(const QJsonObject &payload);
    void onPlanLoaded(const MealPlan &plan);
    void onPlanFailed(std::exception_ptr error);
    QList<MealTransaction> shownActivity() const;
    void rebuildActivity();
    QSet<QString> avoidSet() const;
    void saveAvoid();

    TransportPtr m_transport;
    std::optional<Storage> m_storage;
    bool m_preview = false;
    int m_generation = 0;
    QPointer<SettingsController> m_settings;
    int m_days;
    int m_offset = 0;

    // Every day fetched so far, by date — what the Menu section reads.
    QHash<QDate, DayMenu> m_byDate;
    // The refresh window (the week from today), which Today's sittings are
    // picked from wherever the Menu section has been moved to.
    QList<DayMenu> m_menus;
    // Dates a window fetch has been sent for and not answered, so tapping back
    // three times in a row sends one request rather than three.
    QSet<QDate> m_pending;
    QString m_dayError;

    QString m_meal;
    // Whether the reader picked the sitting; until then it follows the clock.
    bool m_mealPinned = false;
    QStringList m_avoid;
    StationListModel m_stations;
    int m_hidden = 0;

    // The one sitting Today shows, picked off the clock. A second model rather
    // than a filtered view of the stations: the Menu section moves through
    // days independently, and Today must keep showing the next meal while it
    // does.
    MenuListModel m_nextMeal;
    QDate m_nextMealOn;
    std::optional<MenuBlock> m_nextMealBlock;

    // Loads are not the only thing that moves the next sitting on — the clock
    // does too. An app left open through 9:30 should flip from breakfast to
    // lunch without being refreshed. Repeating rather than aimed at the next
    // cutoff, so a phone waking from sleep catches up on the first tick
    // instead of trusting a deadline it slept through.
    QTimer m_nextMealTimer;
    // The day the last tick saw, so midnight is noticed.
    QDate m_tickDay;

    SourceStatus m_menuStatus{MENU_FRESH_SECS};
    bool m_busy = false;
    QString m_error;

    // The meal plan is a *different* source from the menus: a Self-Service
    // page behind the Cedarville sign-in, versus the public menu API. It sits
    // on these screens because that is where a reader expects it, and it loads
    // independently so a failure in one never blanks the other.
    MealPlan m_plan;
    SourceStatus m_planStatus{PLAN_FRESH_SECS};
    bool m_planBusy = false;
    // One for the life of the screen, so the target it reads is kept.
    std::shared_ptr<MealsProvider> m_mealsProvider;

    // The Plan section's history, and its one filter. Filtered here rather
    // than in QML so the list, the summary line and the empty text all agree
    // about what is being shown.
    ActivityListModel m_activity;
    int m_activityFilter = 0;
};

} // namespace mycu
