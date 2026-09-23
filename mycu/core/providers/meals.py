"""Meal plan — meals left, and the dollar balances.

**Verified against the real page**, recaptured 2026-09-22 with
``scripts/discover meals`` after Self-Service redesigned it.

The page is now a Vue app. Its HTML carries no figures at all, only who to look
up, on the mount element:

.. code-block:: html

    <cu-container id="app" data-target-id="0000000" data-target-card="" data-is-admin="0">

and its script then asks a JSON endpoint for the balances:

.. code-block:: text

    GET /CedarInfo/Meals/GetBalanceJson?id=<data-target-id>
    {
      "Status": "ok", "Message": null, "Found": true, "PlanName": "21 Meals",
      "Balances": [
        {"Name": "Board Meals",   "Type": "MEAL",     "Amount": 16,     "IsCurrency": false},
        {"Name": "Flex Dollars",  "Type": "DEBIT",    "Amount": 102.34, "IsCurrency": true},
        {"Name": "Meal Exchange", "Type": "EXCHANGE", "Amount": 16,     "IsCurrency": false}
      ],
      "RecentTransactions": [...], "Admin": null
    }

So :meth:`MealsProvider.fetch` makes two requests, exactly as the page does.
Both are plain same-origin GETs with no token or custom header (the recorded
request had none), so the WebView transport needs nothing new.

.. rubric:: Which balance is which

The old page had "Meal Plan Dining Dollars" (expire at term end) and "Voluntary
Flex Dollars" (purchased, do not expire). The new one lists a single "Flex
Dollars" tender, and it is the *first* of those under a new name: the old page
read $112.08 dining / $0.00 voluntary, and the new one read $102.34 after two
flex purchases totalling exactly $9.74. It maps to
:attr:`~mycu.core.models.MealPlan.dining_dollars`.

The response for that account lists no voluntary balance at all, so a tender is
only treated as voluntary flex when its name says so (:data:`VOLUNTARY_WORDS`).
Otherwise :attr:`~mycu.core.models.MealPlan.flex_dollars` stays ``None`` and the
UI shows "—": that the endpoint left it out is not the same as knowing it is
$0.00.

Tenders are matched by ``Type`` and name rather than by position, so a
reordering upstream cannot shift the values. "Meal Exchange" is not surfaced.
"""

from __future__ import annotations

import json
import logging
import re
from dataclasses import dataclass
from urllib.parse import urlencode

from ..errors import ParseError
from ..minihtml import parse as parse_html
from ..models import MealPlan
from ..transport import Response
from .base import Provider

log = logging.getLogger(__name__)

MEALS_PATH = "/Cedarinfo/Meals"
BALANCE_PATH = "/CedarInfo/Meals/GetBalanceJson"

#: A currency tender whose name contains one of these is the purchased,
#: non-expiring balance; any other currency tender is the plan's own.
VOLUNTARY_WORDS = ("voluntary", "permanent", "purchased", "rollover", "roll over")

#: Which cycle the meal count runs on, read off ``PlanName``. Cedarville's
#: weekly plans are named for their count ("21 Meals", "14 Meals"); the
#: per-term plan is "Block 120". Block is checked first because its name has a
#: number in it too. An unrecognised name gives ``""``, which the UI renders as
#: no qualifier at all rather than a guessed one.
PERIOD_PATTERNS = (
    ("term", re.compile(r"\bblock\b", re.I)),
    ("week", re.compile(r"^\s*\d+\s+meals?\b", re.I)),
)


@dataclass(frozen=True, slots=True)
class MealsTarget:
    """Who the page looks up: the ``data-target-*`` attributes on ``#app``."""

    person_id: str = ""
    card: str = ""


class MealsProvider(Provider[MealPlan]):
    """Meal plan balances for the signed-in student."""

    path = MEALS_PATH
    label = "Meal plan"

    def fetch(self) -> MealPlan:
        """Load the page for its target id, then the balances for that id.

        Both responses are checked for an expired session before parsing, as
        :meth:`Provider.fetch` does for the single-request providers.
        """
        page = self.transport.get(self.path).raise_for_session()
        target = parse_target(page.body)
        response = self.transport.get(balance_path(target)).raise_for_session()
        return self.parse(response)

    def parse(self, response: Response) -> MealPlan:
        return parse_balance(response.body)


def parse_target(body: str) -> MealsTarget:
    """Read ``data-target-id`` / ``data-target-card`` off the page's mount element."""
    if not (body or "").strip():
        raise ParseError("empty response body for the meal-plan page")

    for node in parse_html(body).iter_descendants():
        if "data-target-id" in node.attrs:
            return MealsTarget(
                person_id=node.attrs.get("data-target-id", "").strip(),
                card=node.attrs.get("data-target-card", "").strip(),
            )

    raise ParseError(
        "the meal-plan page has no data-target-id — it has changed shape again; "
        "recapture with `scripts/discover meals`"
    )


def balance_path(target: MealsTarget) -> str:
    """The balance URL for ``target``, sending only the non-empty parameters.

    That is what the page's own ``getJson`` does: the recorded request was
    ``?id=…`` alone, with the empty ``card`` left off.
    """
    params = {k: v for k, v in (("id", target.person_id), ("card", target.card)) if v}
    return f"{BALANCE_PATH}?{urlencode(params)}" if params else BALANCE_PATH


def parse_balance(body: str) -> MealPlan:
    """Turn ``GetBalanceJson`` into a :class:`MealPlan`.

    "No meal plan on file" and "no ID card" are answers, not failures: they
    give an empty plan, rendered as blanks. A response that does not have the
    expected shape raises :class:`ParseError`.
    """
    if not (body or "").strip():
        raise ParseError("empty response body for the meal-plan balances")

    try:
        data = json.loads(body)
    except json.JSONDecodeError as exc:
        raise ParseError(f"meal-plan balances are not JSON: {exc}") from exc
    if not isinstance(data, dict):
        raise ParseError("meal-plan balances are not a JSON object")

    status = str(data.get("Status") or "").lower()
    if status == "error":
        raise ParseError(f"Self-Service could not load the meal plan: {data.get('Message')}")
    if status == "no_card" or (status == "ok" and not data.get("Found")):
        log.info("meals: no plan on file (%s)", status)
        return MealPlan()
    if status != "ok":
        raise ParseError(f"unrecognised meal-plan status {data.get('Status')!r}")

    balances = data.get("Balances")
    if not isinstance(balances, list):
        raise ParseError("meal-plan response has no Balances list — it has changed shape")

    plan_name = str(data.get("PlanName") or "").strip()
    meals = dining = flex = None
    for tender in balances:
        if not isinstance(tender, dict):
            continue
        name = str(tender.get("Name") or "")
        kind = str(tender.get("Type") or "").upper()
        amount = tender.get("Amount")
        if not isinstance(amount, (int, float)) or isinstance(amount, bool):
            continue

        if kind == "MEAL" and meals is None:
            meals = int(amount)
        elif tender.get("IsCurrency") or kind == "DEBIT":
            if any(w in name.lower() for w in VOLUNTARY_WORDS):
                flex = float(amount) if flex is None else flex
            elif dining is None:
                dining = float(amount)

    plan = MealPlan(
        meals_remaining=meals,
        dining_dollars=dining,
        flex_dollars=flex,
        plan_name=plan_name,
        period=_period(plan_name),
    )

    if balances and not plan.has_any:
        names = ", ".join(repr(t.get("Name")) for t in balances if isinstance(t, dict))
        raise ParseError(f"no recognisable meal-plan balances among: {names}")

    log.debug(
        "meals: %s meals, dining=%s, flex=%s, plan=%r",
        plan.meals_remaining, plan.dining_dollars, plan.flex_dollars, plan.plan_name,
    )
    return plan


def _period(plan_name: str) -> str:
    for period, pattern in PERIOD_PATTERNS:
        if pattern.search(plan_name):
            return period
    return ""
