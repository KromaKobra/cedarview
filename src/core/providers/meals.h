// Meal plan — meals left, and the dollar balances.
//
// **Verified against the real page**, recaptured 2026-09-22 with
// `scripts/discover meals` after Self-Service redesigned it.
//
// The page is now a Vue app. Its HTML carries no figures at all, only who to
// look up, on the mount element:
//
//     <cu-container id="app" data-target-id="0000000" data-target-card="" data-is-admin="0">
//
// and its script then asks a JSON endpoint for the balances:
//
//     GET /CedarInfo/Meals/GetBalanceJson?id=<data-target-id>
//     {
//       "Status": "ok", "Message": null, "Found": true, "PlanName": "21 Meals",
//       "Balances": [
//         {"Name": "Board Meals",   "Type": "MEAL",     "Amount": 16,     "IsCurrency": false},
//         {"Name": "Flex Dollars",  "Type": "DEBIT",    "Amount": 102.34, "IsCurrency": true},
//         {"Name": "Meal Exchange", "Type": "EXCHANGE", "Amount": 16,     "IsCurrency": false}
//       ],
//       "RecentTransactions": [...], "Admin": null
//     }
//
// So MealsProvider::fetch() makes two requests, exactly as the page does. Both
// are plain same-origin GETs with no token or custom header (the recorded
// request had none), so the WebView transport needs nothing new.
//
// Which balance is which
// ----------------------
//
// The old page had "Meal Plan Dining Dollars" (expire at term end) and
// "Voluntary Flex Dollars" (purchased, do not expire). The new one lists a
// single "Flex Dollars" tender, and it is the *first* of those under a new
// name: the old page read $112.08 dining / $0.00 voluntary, and the new one
// read $102.34 after two flex purchases totalling exactly $9.74. It maps to
// MealPlan::diningDollars.
//
// The response for that account lists no voluntary balance at all, so a tender
// is only treated as voluntary flex when its name says so (VOLUNTARY_WORDS).
// Otherwise MealPlan::flexDollars stays unreported and the UI shows "—": that
// the endpoint left it out is not the same as knowing it is $0.00.
//
// Tenders are matched by `Type` and name rather than by position, so a
// reordering upstream cannot shift the values. "Meal Exchange" is not surfaced.
//
// Recent activity
// ---------------
//
// `RecentTransactions` is read into MealPlan::transactions for the Dining tab.
// The real capture held 25 rows over nine days, newest first, of three kinds:
// "Board meal" and "Meal exchange" with `Amount: null`, and "Flex purchase"
// with an amount. Rows are sorted here anyway rather than trusted to arrive in
// order, and a malformed row is skipped rather than failing the balances above
// it.

#pragma once

#include "../models.h"
#include "../transport.h"

namespace mycu {

inline const QString MEALS_PATH = QStringLiteral("/Cedarinfo/Meals");
inline const QString BALANCE_PATH = QStringLiteral("/CedarInfo/Meals/GetBalanceJson");

// A currency tender whose name contains one of these is the purchased,
// non-expiring balance; any other currency tender is the plan's own.
inline const QStringList VOLUNTARY_WORDS = {
    QStringLiteral("voluntary"), QStringLiteral("permanent"), QStringLiteral("purchased"),
    QStringLiteral("rollover"),  QStringLiteral("roll over"),
};

// Who the page looks up: the `data-target-*` attributes on `#app`.
struct MealsTarget
{
    QString personId;
    QString card;

    bool operator==(const MealsTarget &) const = default;
};

// Meal plan balances for the signed-in student.
class MealsProvider
{
public:
    static inline const QString path = MEALS_PATH;
    static inline const QString label = QStringLiteral("Meal plan");

    explicit MealsProvider(TransportPtr transport);

    // Load the page for its target id, then the balances for that id.
    //
    // Both responses are checked for an expired session before parsing.
    MealPlan fetch();

    static MealPlan parse(const Response &response);

private:
    TransportPtr m_transport;
};

// Read `data-target-id` / `data-target-card` off the page's mount element.
MealsTarget parseTarget(const QString &body);

// The balance URL for `target`, sending only the non-empty parameters.
//
// That is what the page's own `getJson` does: the recorded request was `?id=…`
// alone, with the empty `card` left off.
QString balancePath(const MealsTarget &target);

// Turn `GetBalanceJson` into a MealPlan.
//
// "No meal plan on file" and "no ID card" are answers, not failures: they give
// an empty plan, rendered as blanks. A response that does not have the expected
// shape throws ParseError.
MealPlan parseBalance(const QString &body);

} // namespace mycu
