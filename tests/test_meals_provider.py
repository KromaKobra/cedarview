"""Meal plan, against a REAL captured page.

Fixture: `tests/fixtures/cedarinfo_meals.html`, trimmed from a signed-in capture
of `selfservice.cedarville.edu/Cedarinfo/Meals` on 2026-09-17. The name and prox
card ID are scrubbed; the balances are verbatim, because they are the thing
being parsed.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from mycu.core.errors import ParseError
from mycu.core.models import MealPlan
from mycu.core.providers.meals import MEALS_PATH, MealsProvider, parse_meals
from mycu.core.transport import FixtureTransport


@pytest.fixture
def page(fixtures_dir: Path) -> str:
    return (fixtures_dir / "cedarinfo_meals.html").read_text()


@pytest.fixture
def plan(page: str) -> MealPlan:
    return parse_meals(page)


# ---------------------------------------------------------------------------
# The three figures
# ---------------------------------------------------------------------------

def test_meals_remaining_this_week(plan) -> None:
    assert plan.meals_remaining == 19


def test_the_two_dollar_balances_are_kept_distinct(plan) -> None:
    """Conflating them would misreport money.

    "Meal Plan Dining Dollars" expire at the end of term; "Voluntary Flex
    Dollars" are purchased separately and do not. They are different balances
    that happen to sit in near-identical sentences.
    """
    assert plan.dining_dollars == 112.08
    assert plan.flex_dollars == 0.00
    assert plan.dining_dollars != plan.flex_dollars


def test_a_zero_balance_is_zero_not_missing(plan) -> None:
    # 0.00 and "not reported" must not collapse into each other.
    assert plan.flex_dollars == 0.0
    assert plan.flex_dollars is not None


def test_the_second_strong_tag_is_not_mistaken_for_an_amount(plan) -> None:
    """Regression guard for the trap in the real markup.

    The Voluntary Flex paragraph reads: "You have <strong>$0.00</strong>
    remaining … These dollars <strong>do not</strong> expire". Reading any
    <strong> rather than the first would eventually yield the string "do not".
    """
    assert plan.flex_dollars == 0.00
    assert isinstance(plan.flex_dollars, float)


def test_the_meal_count_is_never_read_as_money(plan) -> None:
    """19 meals must not become $19.00."""
    assert plan.dining_dollars != 19.0
    assert plan.flex_dollars != 19.0


def test_name_and_prox_card(plan) -> None:
    assert plan.student_name == "Sample Student"
    assert plan.prox_card_id == "0000"


# ---------------------------------------------------------------------------
# Formatting
# ---------------------------------------------------------------------------

def test_money_formatting() -> None:
    assert MealPlan.money(112.08) == "$112.08"
    assert MealPlan.money(0.0) == "$0.00"
    assert MealPlan.money(1234.5) == "$1,234.50"


def test_money_for_a_missing_value_is_blank_not_zero() -> None:
    """"$0.00" for an unknown balance would be a confident lie."""
    assert MealPlan.money(None) == ""


def test_has_any() -> None:
    assert not MealPlan().has_any
    assert MealPlan(meals_remaining=0).has_any
    assert MealPlan(flex_dollars=0.0).has_any


# ---------------------------------------------------------------------------
# Robustness
# ---------------------------------------------------------------------------

def test_paragraphs_are_matched_by_phrase_not_position() -> None:
    """Reordering upstream must not shift the values."""
    html = """
    <div>
      <p>You have <strong>$5.00</strong> remaining in purchased Voluntary Flex Dollars.
         These dollars <strong>do not</strong> expire.</p>
      <p>You have <strong>3</strong> meal(s) remaining in your meal plan for the current week.</p>
      <p>You have <strong>$40.00</strong> remaining in Meal Plan Dining Dollars.</p>
    </div>
    """
    plan = parse_meals(html)
    assert plan.meals_remaining == 3
    assert plan.dining_dollars == 40.00
    assert plan.flex_dollars == 5.00


def test_dining_is_not_matched_by_the_flex_paragraph() -> None:
    """Both sentences end in "Dollars"; only one is dining."""
    html = """
    <div><p>You have <strong>$7.00</strong> remaining in purchased Voluntary Flex Dollars.</p></div>
    """
    plan = parse_meals(html)
    assert plan.flex_dollars == 7.00
    assert plan.dining_dollars is None


def test_a_missing_figure_is_none_not_zero() -> None:
    html = "<div><p>You have <strong>4</strong> meal(s) remaining this week.</p></div>"
    plan = parse_meals(html)
    assert plan.meals_remaining == 4
    assert plan.dining_dollars is None
    assert plan.flex_dollars is None


def test_a_page_with_no_meal_plan_section_raises() -> None:
    with pytest.raises(ParseError, match="discover meals"):
        parse_meals("<html><body><p>Some unrelated page.</p></body></html>")


def test_an_empty_body_raises() -> None:
    with pytest.raises(ParseError):
        parse_meals("")


def test_a_page_with_no_paragraphs_raises() -> None:
    with pytest.raises(ParseError):
        parse_meals("<html><body><div>nothing</div></body></html>")


def test_amounts_with_thousands_separators_parse() -> None:
    html = "<div><p>You have <strong>$1,234.56</strong> remaining in Meal Plan Dining Dollars.</p></div>"
    assert parse_meals(html).dining_dollars == 1234.56


def test_an_amount_without_a_strong_tag_still_parses() -> None:
    """Belt and braces if the markup loses its emphasis tags."""
    html = "<div><p>You have $22.50 remaining in Meal Plan Dining Dollars.</p></div>"
    assert parse_meals(html).dining_dollars == 22.50


def test_unclosed_paragraphs_do_not_merge_into_one() -> None:
    """Cedarville's markup is hand-written; a missing </p> must not fuse lines.

    If the three sentences collapsed into a single "paragraph", the first
    <strong> rule would return 3 for every figure and the dining/flex
    distinction would be lost. Note the inline <span> before the next <p>:
    the paragraph is not the innermost open element at that point.
    """
    html = """
    <fieldset>
      <p>You have <strong>3</strong> meal(s) remaining <span>this week</span>
      <p>You have <strong>$40.00</strong> remaining in Meal Plan Dining Dollars.
      <p>You have <strong>$5.00</strong> remaining in purchased Voluntary Flex Dollars.
    </fieldset>
    """
    plan = parse_meals(html)
    assert plan.meals_remaining == 3
    assert plan.dining_dollars == 40.00
    assert plan.flex_dollars == 5.00


def test_script_contents_are_not_read_as_page_text() -> None:
    """The live page is ~35 KB and full of analytics tags."""
    html = """
    <div>
      <script>var meals = "You have $999.99 remaining in Meal Plan Dining Dollars.";</script>
      <p>You have <strong>$12.00</strong> remaining in Meal Plan Dining Dollars.</p>
    </div>
    """
    assert parse_meals(html).dining_dollars == 12.00


# ---------------------------------------------------------------------------
# The provider
# ---------------------------------------------------------------------------

def test_provider_path_is_on_selfservice() -> None:
    # Not Transact: same origin and same sign-in as chapel.
    assert MealsProvider.path == MEALS_PATH == "/Cedarinfo/Meals"


def test_provider_end_to_end_over_the_fixture(fixtures_dir: Path) -> None:
    plan = MealsProvider(FixtureTransport(fixtures_dir)).fetch()
    assert plan.meals_remaining == 19
    assert plan.dining_dollars == 112.08
    assert plan.flex_dollars == 0.00


# ---------------------------------------------------------------------------
# Which cycle the meal count runs on
#
# Read off the page rather than assumed. Weekly plans and per-term block plans
# both exist, and telling a block-plan holder their meals reset on Sunday would
# be a wrong statement about their own account.
# ---------------------------------------------------------------------------

def test_the_real_page_reports_a_weekly_cycle(fixtures_dir: Path) -> None:
    plan = parse_meals((fixtures_dir / "cedarinfo_meals.html").read_text())
    assert plan.period == "week"
    assert plan.period_text == "this week"
    assert plan.plan_description == "Weekly meal plan"


def test_a_term_plan_is_recognised_as_one() -> None:
    html = "<p>You have <strong>50</strong> meal(s) remaining in your meal plan for the current semester.</p>"
    assert parse_meals(html).period == "term"


def test_the_dollar_paragraphs_do_not_decide_the_cycle() -> None:
    """Every page says "end of the current term" — about the money, not the meals.

    Reading the period off the page as a whole would therefore call every plan
    a term plan, including the weekly one in the committed capture.
    """
    html = """
    <p>You have <strong>19</strong> meal(s) remaining in your meal plan for the current week.</p>
    <p>You have <strong>$112.08</strong> remaining in Meal Plan Dining Dollars.
       These dollars expire at the end of the current term, so use them!</p>
    """
    assert parse_meals(html).period == "week"


def test_an_unrecognised_wording_says_nothing_rather_than_guessing() -> None:
    html = "<p>You have <strong>7</strong> meals remaining in your meal plan.</p>"
    plan = parse_meals(html)
    assert plan.period == ""
    assert plan.period_text == ""
    assert plan.plan_description == ""
    assert plan.meals_remaining == 7        # still parsed; only the cycle is unknown
