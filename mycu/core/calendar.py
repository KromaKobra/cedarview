"""The academic calendar — when a term starts and when it ends.

**This is the one thing in the app that Cedarville does not tell us.** Every
other figure on the summary screen came off a service: the skip ledger, the flex
balances, the menu, the upcoming chapels. None of them carries a term end date.
Self-Service reports a term *code* (``2026FA``) and a *name* ("Fall Semester
2026") and nothing about its boundaries.

So the dates here are entered by hand, and the honest thing to do is say so:

    Fall    Aug 19  ->  Dec 11
    Spring  Jan  5  ->  Apr 30

They are stored as ``(month, day)`` rather than full dates, because the
semester's shape repeats and its year does not. That makes the table correct
next year without an edit, at the cost of not modelling the year-to-year drift
of a few days that a real academic calendar has. For a countdown on a summary
screen, "84 days left" being off by two in September is not a number anyone acts
on differently; when it matters — the last fortnight — the drift is smaller than
the number.

Between terms there is no answer, and this module returns ``None`` rather than
inventing one. See :func:`current_term`.

Nested under ``mycu.core`` on purpose: a top-level ``calendar.py`` would shadow
the stdlib module of that name. Same reasoning as ``mycu.platform`` — see
``tests/test_core_is_qt_free.py``.
"""

from __future__ import annotations

from dataclasses import dataclass
from datetime import date

__all__ = [
    "TermWindow",
    "TERMS",
    "FALL",
    "SPRING",
    "current_term",
    "next_term_start",
]


@dataclass(frozen=True, slots=True)
class TermWindow:
    """One semester, as a pair of ``(month, day)`` bounds.

    Both bounds are **inclusive**: Dec 11 is the last day of the fall term, not
    the first day after it.
    """

    name: str
    start: tuple[int, int]
    end: tuple[int, int]

    def start_in(self, year: int) -> date:
        return date(year, *self.start)

    def end_in(self, year: int) -> date:
        return date(year, *self.end)

    def contains(self, day: date) -> bool:
        return self.start_in(day.year) <= day <= self.end_in(day.year)

    def days_left(self, day: date) -> int:
        """Whole days from ``day`` to the last day of term, floored at 0.

        The last day of term counts as 0 left, not 1: on Dec 11 the semester is
        not "1 day" away from being over, it is over today.
        """
        return max(0, (self.end_in(day.year) - day).days)

    def total_days(self, year: int) -> int:
        """What :meth:`days_left` reads on the first day of term.

        Not the inclusive length: the last day counts as 0 left, so the
        countdown runs from this figure down to 0, and "N/N days left" on the
        first day is the figure you would expect to see.
        """
        return (self.end_in(year) - self.start_in(year)).days

    def elapsed_fraction(self, day: date) -> float:
        """How much of the term is behind us, 0.0–1.0.

        Clamped. Empty on the first day, full on the last — the semester bar
        fills as the term runs, the opposite of the chapel-skip bar above it,
        which empties as skips are spent.
        """
        total = self.total_days(day.year)
        if total <= 0:
            return 0.0
        return max(0.0, min(1.0, 1.0 - self.days_left(day) / total))


#: Fall runs inside one calendar year; spring runs inside the next one. Neither
#: straddles New Year, which is what lets both be plain ``(month, day)`` pairs.
FALL = TermWindow("Fall", start=(8, 19), end=(12, 11))
SPRING = TermWindow("Spring", start=(1, 5), end=(4, 30))

#: In calendar order within a year, which is what :func:`next_term_start` walks.
TERMS = (SPRING, FALL)


def current_term(day: date | None = None) -> TermWindow | None:
    """The term ``day`` falls in, or ``None`` between terms.

    ``None`` is a real answer, not a failure: May through mid-August and the
    fortnight after fall finals are genuinely not part of a semester, and a
    countdown that kept running through the summer would be counting down to
    nothing.
    """
    day = day or date.today()
    for term in TERMS:
        if term.contains(day):
            return term
    return None


def next_term_start(day: date | None = None) -> tuple[TermWindow, date] | None:
    """The next term to begin after ``day``, and the date it begins.

    Returns ``None`` when ``day`` is inside a term — there is no "next" to
    report while you are in one. Used only for the between-terms state of the
    summary card, which says "Spring starts Mon, Jan 5" instead of a countdown.
    """
    day = day or date.today()
    if current_term(day) is not None:
        return None

    # This year's remaining starts first, then January of next year. Two years
    # is always enough: the gap between any two term starts is under twelve
    # months, so the first hit is never further out than next spring.
    for year in (day.year, day.year + 1):
        for term in TERMS:
            starts = term.start_in(year)
            if starts > day:
                return term, starts
    return None  # pragma: no cover — unreachable while TERMS is non-empty
