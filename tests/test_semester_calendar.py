"""The academic calendar, at its edges.

Every bug this module can have is a boundary bug — off by one at the start of
term, off by one at the end, or the wrong answer in the gap between them. The
countdown is also the one figure on the summary screen that cannot be checked
against a live service, so this is the only thing standing behind it.

Note what is *not* asserted: the exact number of days left today. That test
would pass once and then fail every day afterwards.
"""

from __future__ import annotations

from datetime import date

import pytest

from mycu.core.calendar import FALL, SPRING, current_term, next_term_start


# ---------------------------------------------------------------------------
# Which term a date falls in
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    "day, expected",
    [
        # Fall: Aug 19 -> Dec 11, both inclusive.
        (date(2026, 8, 18), None),
        (date(2026, 8, 19), "Fall"),
        (date(2026, 10, 1), "Fall"),
        (date(2026, 12, 11), "Fall"),
        (date(2026, 12, 12), None),
        # Spring: Jan 5 -> Apr 30, both inclusive.
        (date(2027, 1, 4), None),
        (date(2027, 1, 5), "Spring"),
        (date(2027, 4, 30), "Spring"),
        (date(2027, 5, 1), None),
        # The two breaks, well inside.
        (date(2026, 12, 25), None),
        (date(2027, 7, 4), None),
    ],
)
def test_which_term_a_day_falls_in(day: date, expected: str | None) -> None:
    term = current_term(day)
    assert (term.name if term else None) == expected


# ---------------------------------------------------------------------------
# Days left
# ---------------------------------------------------------------------------


def test_the_last_day_of_term_has_zero_days_left() -> None:
    """Not one. On Dec 11 the semester is over today, not tomorrow."""
    assert FALL.days_left(date(2026, 12, 11)) == 0


def test_the_day_before_the_end_has_one_day_left() -> None:
    assert FALL.days_left(date(2026, 12, 10)) == 1


def test_days_left_counts_calendar_days_not_class_days() -> None:
    # Sep 18 -> Dec 11 is 84 days, weekends and Thanksgiving included. The card
    # says "days left", and this is what a reader would count on a wall
    # calendar.
    assert FALL.days_left(date(2026, 9, 18)) == 84


def test_days_left_never_goes_negative() -> None:
    """Belt and braces: the viewmodel only asks while in term, but the floor
    means a caller that does not check cannot produce "-12 days left"."""
    assert FALL.days_left(date(2026, 12, 31)) == 0


def test_spring_is_measured_inside_its_own_year() -> None:
    assert SPRING.days_left(date(2027, 4, 1)) == 29


# ---------------------------------------------------------------------------
# The bar
# ---------------------------------------------------------------------------


def test_the_fraction_is_full_on_the_first_day_and_empty_on_the_last() -> None:
    assert FALL.remaining_fraction(date(2026, 8, 19)) == pytest.approx(1.0, abs=0.01)
    assert FALL.remaining_fraction(date(2026, 12, 11)) == 0.0


def test_the_fraction_is_clamped_outside_the_term() -> None:
    assert 0.0 <= FALL.remaining_fraction(date(2026, 6, 1)) <= 1.0
    assert 0.0 <= FALL.remaining_fraction(date(2026, 12, 31)) <= 1.0


def test_the_fraction_falls_as_the_term_runs() -> None:
    """The direction matters more than any single value.

    This bar sits directly under the chapel-skip bar, and both must mean "how
    much is left". A fraction that rose would make two adjacent identical bars
    say opposite things.
    """
    readings = [
        FALL.remaining_fraction(date(2026, m, 1)) for m in (9, 10, 11, 12)
    ]
    assert readings == sorted(readings, reverse=True)


# ---------------------------------------------------------------------------
# Between terms
# ---------------------------------------------------------------------------


def test_there_is_no_next_term_while_you_are_in_one() -> None:
    assert next_term_start(date(2026, 10, 1)) is None


def test_the_christmas_break_looks_forward_into_the_next_year() -> None:
    """The one case a naive implementation gets wrong."""
    upcoming = next_term_start(date(2026, 12, 20))
    assert upcoming is not None
    term, starts = upcoming
    assert term.name == "Spring"
    assert starts == date(2027, 1, 5)


def test_the_first_days_of_january_still_point_at_this_year_s_spring() -> None:
    upcoming = next_term_start(date(2027, 1, 1))
    assert upcoming is not None
    term, starts = upcoming
    assert term.name == "Spring"
    assert starts == date(2027, 1, 5)


def test_the_summer_points_at_the_fall() -> None:
    upcoming = next_term_start(date(2027, 5, 15))
    assert upcoming is not None
    term, starts = upcoming
    assert term.name == "Fall"
    assert starts == date(2027, 8, 19)


def test_the_two_terms_do_not_overlap_anywhere_in_a_year() -> None:
    """Every day of a leap year belongs to at most one term."""
    day = date(2028, 1, 1)
    while day.year == 2028:
        matches = [t for t in (FALL, SPRING) if t.contains(day)]
        assert len(matches) <= 1, f"{day} is in {[t.name for t in matches]}"
        day = day.fromordinal(day.toordinal() + 1)
