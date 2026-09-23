// The academic calendar — when a term starts and when it ends.
//
// **This is the one thing in the app that Cedarville does not tell us.** Every
// other figure on the summary screen came off a service: the skip ledger, the
// flex balances, the menu, the upcoming chapels. None of them carries a term
// end date. Self-Service reports a term *code* (`2026FA`) and a *name* ("Fall
// Semester 2026") and nothing about its boundaries.
//
// So the dates here are entered by hand, and the honest thing to do is say so:
//
//     Fall    Aug 19  ->  Dec 11
//     Spring  Jan  5  ->  Apr 30
//
// They are stored as (month, day) rather than full dates, because the
// semester's shape repeats and its year does not. That makes the table correct
// next year without an edit, at the cost of not modelling the year-to-year
// drift of a few days that a real academic calendar has. For a countdown on a
// summary screen, "84 days left" being off by two in September is not a number
// anyone acts on differently; when it matters — the last fortnight — the drift
// is smaller than the number.
//
// Between terms there is no answer, and this returns nothing rather than
// inventing one. See currentTerm().

#pragma once

#include <QDate>
#include <QString>

#include <optional>
#include <utility>

namespace mycu {

// One semester, as a pair of (month, day) bounds.
//
// Both bounds are **inclusive**: Dec 11 is the last day of the fall term, not
// the first day after it.
struct TermWindow
{
    QString name;
    int startMonth = 1;
    int startDay = 1;
    int endMonth = 1;
    int endDay = 1;

    QDate startIn(int year) const { return QDate(year, startMonth, startDay); }
    QDate endIn(int year) const { return QDate(year, endMonth, endDay); }

    bool contains(QDate day) const;

    // Whole days from `day` to the last day of term, floored at 0.
    //
    // The last day of term counts as 0 left, not 1: on Dec 11 the semester is
    // not "1 day" away from being over, it is over today.
    int daysLeft(QDate day) const;

    // What daysLeft() reads on the first day of term.
    //
    // Not the inclusive length: the last day counts as 0 left, so the
    // countdown runs from this figure down to 0, and "N/N days left" on the
    // first day is the figure you would expect to see.
    int totalDays(int year) const;

    // How much of the term is behind us, 0.0–1.0.
    //
    // Clamped. Empty on the first day, full on the last — the semester bar
    // fills as the term runs, the opposite of the chapel-skip bar above it,
    // which empties as skips are spent.
    double elapsedFraction(QDate day) const;

    bool operator==(const TermWindow &) const = default;
};

// Fall runs inside one calendar year; spring runs inside the next one. Neither
// straddles New Year, which is what lets both be plain (month, day) pairs.
inline const TermWindow FALL{QStringLiteral("Fall"), 8, 19, 12, 11};
inline const TermWindow SPRING{QStringLiteral("Spring"), 1, 5, 4, 30};

// The term `day` falls in, or nothing between terms.
//
// Nothing is a real answer, not a failure: May through mid-August and the
// fortnight after fall finals are genuinely not part of a semester, and a
// countdown that kept running through the summer would be counting down to
// nothing.
std::optional<TermWindow> currentTerm(QDate day = QDate::currentDate());

// The next term to begin after `day`, and the date it begins.
//
// Nothing when `day` is inside a term — there is no "next" to report while you
// are in one. Used only for the between-terms state of the summary card, which
// says "Spring starts Mon, Jan 5" instead of a countdown.
std::optional<std::pair<TermWindow, QDate>> nextTermStart(QDate day = QDate::currentDate());

} // namespace mycu
