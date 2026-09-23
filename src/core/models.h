// Domain types.
//
// Deliberately not a mirror of whatever Ellucian sends. These are the shapes the
// QML layer binds to; the provider's job is to translate. When Cedarville
// renames a column, only the provider changes.
//
// "Not reported" is spelled out rather than defaulted: std::optional for
// numbers, and an invalid QDateTime/QDate for dates. A confident zero for an
// unknown figure would be a lie on screen.

#pragma once

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace mycu {

// One component of the skip allowance.
//
// Real example: a base allowance of 17 plus a "Manual Arrangement" of 1,
// summing to the reported total of 18.
struct AllowanceLine
{
    QString reason;
    int count = 0;
    QString description;

    bool operator==(const AllowanceLine &) const = default;
};

// One line of the chapel-skip ledger.
//
// This is a **ledger, not an attendance register** — a distinction that
// matters. Entries are not "you were absent on this date"; they are movements
// against the skip balance:
//
// * `EntryType: "Chapel Skip"`, `Count: 1` — an absence was recorded.
// * `EntryType: "Manual Adjustment"`, `Count: -1` — a skip was given back (in
//   the captured data: "Had ID replaced").
//
// So `on` is genuinely invalid for adjustments: they are not tied to a chapel.
// when() exists so the UI always has something true to show.
struct ChapelLedgerEntry
{
    QDateTime on; // invalid = no chapel date
    int count = 0;
    QString entryType;
    QString reason;
    QDateTime createdAt; // invalid = not reported
    bool canRemove = false;

    bool isSkip() const { return count > 0; }

    // A date to show, falling back to the reason for undated adjustments.
    QString when() const;

    bool operator==(const ChapelLedgerEntry &) const = default;
};

// Everything the chapel screen needs.
//
// IMPORTANT: `used`, `total` and `remaining` are **reported by Cedarville and
// never computed here.** That is not a stylistic preference — deriving them
// gives the wrong answer. In the captured data the server reports
// `SkipsUsed: 2` while the ledger's counts sum to `1`, because the `-1` manual
// adjustment is *also* represented as a `+1` line in the allowance breakdown
// (17 base + 1 arrangement = 18 total; 18 - 2 = 16 remaining). Both halves are
// consistent on the server's terms and inconsistent on ours. The school's
// arithmetic is the one that counts.
struct ChapelSummary
{
    std::optional<int> used;
    std::optional<int> total;
    std::optional<int> remaining;

    QString term;
    QString termName;
    QString studentName;
    QString studentId;

    QList<AllowanceLine> allowance;
    QStringList requirementReasons;
    bool isRequiredToAttend = true;
    bool isInGoodStanding = true;
    QString status;

    QList<ChapelLedgerEntry> entries;
    QList<QJsonObject> fines;

    // The nicer of the two term spellings — "Fall Semester 2026" over "2026FA".
    QString label() const { return !termName.isEmpty() ? termName : term; }
};

// ---------------------------------------------------------------------------
// Dining
//
// Unlike the chapel models, these are NOT guesses. They mirror the real,
// verified response of https://diningdata.cedarville.edu/api/menus?days=N,
// captured 2026-09-16 and committed to tests/fixtures/.
// ---------------------------------------------------------------------------

// The dining hall station this app is actually about. The API calls it a
// "venue"; The Commons has about a dozen (Grille, Italian, Habanero, SubZone…)
// and this is the one worth a screen of its own.
inline const QString HOME_COOKING = QStringLiteral("Home Cooking");

// `slot` values, in the order the sittings happen. Anything else — in practice
// `"anytime"`, for stations that run all day — sorts last.
//
// **Order on `slot`, never on `meal`.** Checked against the real payload:
// `slot` is always one of these four, but `meal` is a free-text label that is
// sometimes the sitting ("Breakfast"), sometimes null (Grille, Italian,
// SubZone…), and sometimes a sub-station name ("Deli" under Allergen Aware,
// "yogurt bar" under Breakfast All Day). Keying the sort on `meal` put "yogurt
// bar" — a breakfast item — after dinner.
inline const QStringList SLOT_ORDER = {
    QStringLiteral("breakfast"),
    QStringLiteral("lunch"),
    QStringLiteral("dinner"),
};

// Pretty names for the slots, for when `meal` is null and we still want a
// heading.
inline const QHash<QString, QString> SLOT_LABELS = {
    {QStringLiteral("breakfast"), QStringLiteral("Breakfast")},
    {QStringLiteral("lunch"), QStringLiteral("Lunch")},
    {QStringLiteral("dinner"), QStringLiteral("Dinner")},
    {QStringLiteral("anytime"), QStringLiteral("All day")},
};

// One dish.
//
// `allergens` comes back from the API as a list of `{url, alt}` icon objects;
// only the `alt` text ("dairy", "gluten", "egg", "soy"…) is worth keeping, so
// the icon URLs are dropped at parse time.
struct MenuItem
{
    QString name;
    QStringList allergens;

    QString allergenText() const { return allergens.join(QStringLiteral(", ")); }

    bool operator==(const MenuItem &) const = default;
};

// One station, at one sitting, on one day.
//
// `slot` is the machine-readable sitting (breakfast/lunch/dinner/anytime);
// `meal` is the upstream's free-text label for the block and may be empty. Use
// heading() for display.
struct MenuBlock
{
    QString venue;
    QString meal;
    QString slot;
    QList<MenuItem> items;

    // Breakfast, then lunch, then dinner; all-day stations last.
    //
    // The API does not return blocks in chronological order, and a menu
    // listing dinner before breakfast reads as a bug even when every item on
    // it is correct. Keys on `slot` — see SLOT_ORDER for why `meal` is the
    // wrong field for this.
    int sortKey() const;

    // What to show above this block.
    //
    // Prefers the upstream's own label, because it is sometimes more specific
    // than the slot ("Deli", "yogurt bar"), and falls back to the slot when it
    // is missing — which is the common case for all-day stations.
    QString heading() const;

    bool operator==(const MenuBlock &) const = default;
};

// Every block served on one date.
struct DayMenu
{
    QDate on;
    QList<MenuBlock> blocks;

    // This day's blocks for one station, in meal order.
    QList<MenuBlock> forVenue(const QString &venue = HOME_COOKING) const;

    // Every station serving that day, deduplicated, in first-seen order.
    QStringList venues() const;

    bool operator==(const DayMenu &) const = default;
};

// ---------------------------------------------------------------------------
// Chapel schedule
//
// Also not guesses: the shape below was read off the live v2 API, which was
// found by watching cedarville.edu/chapel's own network traffic
// (`scripts/discover chapel-schedule`). Real capture in tests/fixtures/.
// ---------------------------------------------------------------------------

// A scheduled chapel that has not happened yet.
//
// `speakers` is genuinely often empty — "Worship Chapel", "SGA" and the like
// are real scheduled chapels with no named speaker. who() exists so the UI
// never has to render an empty string.
struct UpcomingChapel
{
    QDateTime startsAt; // invalid = undated
    QString title;
    QStringList speakers;
    QString description;
    bool willLivestream = false;

    // Who is speaking, or the event's own name when nobody is named.
    QString who() const;

    // Whether the title adds nothing beyond what who() already says.
    //
    // The API commonly sets `Title` to the speaker's name verbatim, and showing
    // "Garrett Kell — Garrett Kell" looks like a bug.
    //
    // Compared against who(), not against the speaker list, because who() is
    // what the UI actually renders. With the list it read as "does the title
    // repeat the speakers?", which is false whenever there are no speakers —
    // and in exactly that case who() has already fallen back to the title.
    // Seen on the phone: an unnamed "Worship Chapel" rendered as "Worship
    // Chapel" above "Worship Chapel".
    bool isSameAsTitle() const;

    QDate on() const { return startsAt.isValid() ? startsAt.date() : QDate(); }

    bool operator==(const UpcomingChapel &) const = default;
};

// ---------------------------------------------------------------------------
// Meal plan
//
// Verified against the real endpoint (selfservice.cedarville.edu/CedarInfo/
// Meals/GetBalanceJson, recaptured 2026-09-22 after the page became a Vue app).
// Real capture (trimmed, scrubbed) in tests/fixtures/.
// ---------------------------------------------------------------------------

// One row of the meal plan's recent activity: a swipe, or money moving.
//
// The real feed has three kinds, all seen in the 2026-09-22 capture: "Board
// meal" and "Meal exchange" (`Amount: null`) and "Flex purchase" (with an
// amount). `IsDeposit` exists for money going *in*; no deposit has been seen
// yet, so it is handled but not verified.
struct MealTransaction
{
    QDateTime at; // invalid = undated
    QString activity;
    // "Breakfast" / "Lunch" / "Dinner", or "".
    QString mealPeriod;
    // Nothing for a swipe, which moves no money — not 0.0.
    std::optional<double> amount;
    bool isDeposit = false;

    // Whether flex dollars moved: any row with an amount, either way.
    bool isFlex() const;

    // "−$3.74" spent, "+$20.00" deposited, "" for a swipe.
    QString amountText() const;

    bool operator==(const MealTransaction &) const = default;
};

// The balances the meal-plan page reports.
//
// IMPORTANT: **There are two different dollar balances**, and conflating them
// would misreport money:
//
// * diningDollars — the plan's own dollars. The page called them "Meal Plan
//   Dining Dollars" until September 2026 and now calls them "Flex Dollars".
//   They **expire at the end of the term** (the app's "Temporary Flex").
// * flexDollars — purchased "Voluntary Flex Dollars", bought separately, which
//   **do not expire** ("Permanent Flex").
//
// Both are surfaced, each labelled with its own expiry, rather than picking one
// and hoping.
//
// Every field is optional: a student with no meal plan, or a page that changes
// shape, must render as "not reported" rather than as a confident zero.
struct MealPlan
{
    std::optional<int> mealsRemaining;
    std::optional<double> diningDollars;
    std::optional<double> flexDollars;

    // The plan as Self-Service names it — "21 Meals", "Block 120" — or "".
    QString planName;

    // Which cycle mealsRemaining counts down — "week" or "term", and "" when
    // unknown. Read off planName, never assumed: block plans are per-term and
    // telling a term-plan holder their meals reset on Sunday would be wrong.
    QString period;

    // Recent activity, newest first. Not counted by hasAny(): a plan with no
    // balances is not made "reported" by its history.
    QList<MealTransaction> transactions;

    // How long mealsRemaining lasts, in words — "this week" / "this term" / ""
    // when unknown. Here rather than in the QML because whether the figure is
    // weekly at all is a fact about the data.
    QString periodText() const;

    // "21 Meals per week" / "Block 120" / "Weekly meal plan" / "" — the plan's
    // name when Self-Service gave one, with the cycle spelled out when the name
    // alone does not say it.
    QString planDescription() const;

    bool hasAny() const;

    // 112.08 -> "$112.08"; nothing -> "" (never "$0.00").
    static QString money(std::optional<double> value);
};

} // namespace mycu
