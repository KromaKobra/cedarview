"""Meal plan, against a REAL captured page and endpoint.

Fixtures, both from a signed-in capture on 2026-09-22 (``scripts/discover meals``):

* ``tests/fixtures/cedarinfo_meals.html`` — the Vue page, trimmed. It carries
  no figures, only ``data-target-id`` (scrubbed to 0000000).
* ``tests/fixtures/cedarinfo_meals_getbalancejson.json`` — what
  ``/CedarInfo/Meals/GetBalanceJson`` returned. Balances and plan name are
  verbatim, because they are the thing being parsed; the transaction rows are
  replaced with made-up ones of the same shape.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from mycu.core.errors import ParseError, SessionExpired
from mycu.core.models import MealPlan
from mycu.core.providers.meals import (
    BALANCE_PATH,
    MEALS_PATH,
    MealsProvider,
    MealsTarget,
    balance_path,
    parse_balance,
    parse_target,
)
from mycu.core.transport import FixtureTransport, Response


@pytest.fixture
def balance(fixtures_dir: Path) -> dict:
    return json.loads((fixtures_dir / "cedarinfo_meals_getbalancejson.json").read_text())


@pytest.fixture
def plan(balance: dict) -> MealPlan:
    return parse_balance(json.dumps(balance))


def _with_balances(balance: dict, *tenders: dict, **fields) -> str:
    return json.dumps({**balance, "Balances": list(tenders), **fields})


def _tender(name: str, kind: str, amount, currency: bool) -> dict:
    return {"Name": name, "Type": kind, "Amount": amount, "IsCurrency": currency}


# ---------------------------------------------------------------------------
# The figures
# ---------------------------------------------------------------------------

def test_meals_remaining(plan) -> None:
    assert plan.meals_remaining == 16


def test_flex_dollars_are_the_plans_expiring_balance(plan) -> None:
    """The new page's "Flex Dollars" is the old "Meal Plan Dining Dollars".

    Same account, five days apart: $112.08 before, $102.34 after two flex
    purchases of $3.74 and $6.00. It expires at term end, so it is
    ``dining_dollars`` (Temporary Flex), not the purchased kind.
    """
    assert plan.dining_dollars == 102.34
    assert isinstance(plan.dining_dollars, float)


def test_an_absent_voluntary_balance_is_not_reported_rather_than_zero(plan) -> None:
    """The endpoint lists no voluntary tender; "$0.00" would be a guess."""
    assert plan.flex_dollars is None
    assert MealPlan.money(plan.flex_dollars) == ""


def test_meal_exchanges_are_not_mistaken_for_meals_or_money(plan) -> None:
    # "Meal Exchange" also has an Amount of 16 in the capture; it must not be
    # what fills either figure, whatever the order.
    assert plan.dining_dollars != 16.0


def test_a_voluntary_balance_is_recognised_by_name(balance) -> None:
    plan = parse_balance(_with_balances(
        balance,
        _tender("Voluntary Flex Dollars", "DEBIT", 25, True),
        _tender("Flex Dollars", "DEBIT", 80.5, True),
        _tender("Board Meals", "MEAL", 3, False),
    ))
    assert plan.flex_dollars == 25.0
    assert plan.dining_dollars == 80.5
    assert plan.meals_remaining == 3


def test_tenders_are_matched_by_type_not_position(balance) -> None:
    tenders = list(reversed(balance["Balances"]))
    plan = parse_balance(_with_balances(balance, *tenders))
    assert plan.meals_remaining == 16
    assert plan.dining_dollars == 102.34


def test_a_zero_balance_is_zero_not_missing(balance) -> None:
    plan = parse_balance(_with_balances(balance, _tender("Flex Dollars", "DEBIT", 0, True)))
    assert plan.dining_dollars == 0.0
    assert plan.dining_dollars is not None


def test_a_null_amount_is_not_reported(balance) -> None:
    plan = parse_balance(_with_balances(
        balance,
        _tender("Flex Dollars", "DEBIT", None, True),
        _tender("Board Meals", "MEAL", 4, False),
    ))
    assert plan.dining_dollars is None
    assert plan.meals_remaining == 4


# ---------------------------------------------------------------------------
# Plan name and cycle
#
# Read off PlanName rather than assumed. Weekly plans and per-term block plans
# both exist, and telling a block-plan holder their meals reset on Sunday would
# be a wrong statement about their own account.
# ---------------------------------------------------------------------------

def test_the_real_plan_is_weekly(plan) -> None:
    assert plan.plan_name == "21 Meals"
    assert plan.period == "week"
    assert plan.period_text == "this week"
    assert plan.plan_description == "21 Meals per week"


def test_a_block_plan_is_per_term(balance) -> None:
    plan = parse_balance(_with_balances(
        balance, _tender("Board Meals", "MEAL", 90, False), PlanName="Block 120",
    ))
    assert plan.period == "term"
    assert plan.plan_description == "Block 120"


def test_an_unrecognised_plan_name_says_nothing_about_the_cycle(balance) -> None:
    plan = parse_balance(_with_balances(
        balance, _tender("Board Meals", "MEAL", 7, False), PlanName="Commuter Special",
    ))
    assert plan.period == ""
    assert plan.period_text == ""
    assert plan.plan_description == "Commuter Special"
    assert plan.meals_remaining == 7        # still parsed; only the cycle is unknown


def test_no_plan_name_falls_back_to_the_cycle_or_nothing() -> None:
    assert MealPlan(period="week").plan_description == "Weekly meal plan"
    assert MealPlan().plan_description == ""


# ---------------------------------------------------------------------------
# Answers that are not balances
# ---------------------------------------------------------------------------

def test_no_plan_on_file_is_an_empty_plan_not_an_error(balance) -> None:
    plan = parse_balance(json.dumps({**balance, "Found": False, "Balances": []}))
    assert not plan.has_any


def test_no_id_card_is_an_empty_plan_not_an_error() -> None:
    plan = parse_balance(json.dumps({"Status": "no_card", "Message": "No card", "Found": False}))
    assert not plan.has_any


def test_a_server_error_is_reported_with_its_message() -> None:
    with pytest.raises(ParseError, match="database unavailable"):
        parse_balance(json.dumps({"Status": "error", "Message": "database unavailable"}))


@pytest.mark.parametrize("body", ["", "   ", "<html>login</html>", "[]"])
def test_a_non_answer_raises(body: str) -> None:
    with pytest.raises(ParseError):
        parse_balance(body)


def test_a_reshaped_response_raises_rather_than_showing_blanks(balance) -> None:
    reshaped = {k: v for k, v in balance.items() if k != "Balances"}
    with pytest.raises(ParseError, match="Balances"):
        parse_balance(json.dumps({**reshaped, "Tenders": balance["Balances"]}))


def test_unrecognised_tenders_raise_and_name_themselves(balance) -> None:
    with pytest.raises(ParseError, match="Swipes"):
        parse_balance(_with_balances(balance, _tender("Swipes", "SWIPE", 4, False)))


# ---------------------------------------------------------------------------
# The page: only who to look up
# ---------------------------------------------------------------------------

def test_the_target_is_read_off_the_real_page(fixtures_dir: Path) -> None:
    target = parse_target((fixtures_dir / "cedarinfo_meals.html").read_text())
    assert target == MealsTarget(person_id="0000000", card="")


def test_a_page_without_a_target_raises() -> None:
    with pytest.raises(ParseError, match="data-target-id"):
        parse_target("<html><body><p>You have <strong>19</strong> meals.</p></body></html>")


def test_the_balance_url_sends_only_what_the_page_sends() -> None:
    # The recorded request was ?id=… alone: the empty card is left off.
    assert balance_path(MealsTarget("0000000", "")) == f"{BALANCE_PATH}?id=0000000"
    assert balance_path(MealsTarget("", "12345")) == f"{BALANCE_PATH}?card=12345"
    assert balance_path(MealsTarget()) == BALANCE_PATH


# ---------------------------------------------------------------------------
# The provider
# ---------------------------------------------------------------------------

def test_provider_path_is_on_selfservice() -> None:
    # Not Transact: same origin and same sign-in as chapel.
    assert MealsProvider.path == MEALS_PATH == "/Cedarinfo/Meals"
    assert BALANCE_PATH.startswith("/CedarInfo/Meals/")


def test_provider_end_to_end_over_the_fixtures(fixtures_dir: Path) -> None:
    plan = MealsProvider(FixtureTransport(fixtures_dir)).fetch()
    assert plan.meals_remaining == 16
    assert plan.dining_dollars == 102.34
    assert plan.flex_dollars is None
    assert plan.plan_description == "21 Meals per week"


class _RecordingTransport:
    """Serves the fixtures and remembers what was asked for, in order."""

    def __init__(self, fixtures_dir: Path, login_on: str = "") -> None:
        self.inner = FixtureTransport(fixtures_dir)
        self.login_on = login_on
        self.paths: list[str] = []

    def get(self, path: str) -> Response:
        self.paths.append(path)
        if self.login_on and self.login_on in path:
            return Response(status=200, url="https://login.microsoftonline.com/x/saml2",
                            body="<html>SAMLRequest</html>", headers={})
        return self.inner.get(path)


def test_provider_asks_for_the_page_then_the_id_it_names(fixtures_dir: Path) -> None:
    transport = _RecordingTransport(fixtures_dir)
    MealsProvider(transport).fetch()
    assert transport.paths == [MEALS_PATH, f"{BALANCE_PATH}?id=0000000"]


@pytest.mark.parametrize("expires_on", [MEALS_PATH, BALANCE_PATH])
def test_an_expired_session_on_either_request_is_reported_as_one(
    fixtures_dir: Path, expires_on: str,
) -> None:
    with pytest.raises(SessionExpired):
        MealsProvider(_RecordingTransport(fixtures_dir, login_on=expires_on)).fetch()


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
