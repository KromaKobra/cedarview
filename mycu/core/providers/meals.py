"""Meal plan — meals left this week, and the two dollar balances.

**Verified against the real page**, captured 2026-09-17 with
``scripts/discover meals``.

    GET https://selfservice.cedarville.edu/Cedarinfo/Meals

Same origin and the same Microsoft sign-in as chapel, so it needs no new auth
and reuses the WebView transport unchanged. (An earlier guess had this on
Transact's cardholder site, which would have meant a second, separate
credential. It does not live there.)

.. rubric:: The page

Server-rendered, and — worth saying plainly — **there is no table and no JSON**.
The numbers are prose inside ``<strong>`` tags in one ``<fieldset>``:

.. code-block:: html

    <fieldset>
      <legend><strong>Meal Plan Information</strong></legend>
      <h5>Sample Student</h5>
      <p>You have <strong>19</strong> meal(s) remaining in your meal plan for the current week.</p>
      <p>You have <strong>$112.08</strong> remaining in Meal Plan Dining Dollars.
         These dollars expire at the <span style="color:red;">end of the current term</span>, so use them!</p>
      <p>You have <strong>$0.00</strong> remaining in purchased Voluntary Flex Dollars.
         These dollars <strong>do not</strong> expire at the end of the current term.</p>
      <p>Your Prox Card ID: <strong>0000</strong></p>
    </fieldset>

.. rubric:: Two traps in that markup

1. **Two different dollar balances.** "Meal Plan Dining Dollars" expire at the
   end of the term; "Voluntary Flex Dollars" are purchased separately and do
   not. Reporting one as "your balance" would misstate money. Both are parsed
   and kept distinct — see :class:`~mycu.core.models.MealPlan`.
2. **The Voluntary Flex paragraph contains a second ``<strong>``** — the
   ``<strong>do not</strong>`` in "These dollars **do not** expire". Taking
   "a ``<strong>`` in the paragraph" would sometimes yield the string
   ``"do not"``. Only the *first* ``<strong>`` in each paragraph is read.

Paragraphs are matched by their distinguishing phrase rather than by position,
so inserting or reordering a line upstream does not silently shift the values.
"""

from __future__ import annotations

import logging
import re

from ..errors import ParseError
from ..minihtml import Element, parse as parse_html
from ..models import MealPlan
from ..transport import Response
from .base import Provider

log = logging.getLogger(__name__)

MEALS_PATH = "/Cedarinfo/Meals"

#: Phrases that identify each paragraph. Matched case-insensitively against the
#: paragraph's visible text. Ordered most-specific-first within each group so a
#: rewording upstream degrades to "not reported" rather than to a wrong number.
MEALS_MARKERS = ("meal(s) remaining", "meals remaining", "remaining in your meal plan")
DINING_MARKERS = ("meal plan dining dollars", "dining dollars")
FLEX_MARKERS = ("voluntary flex dollars", "flex dollars")
PROX_MARKERS = ("prox card id",)

#: Which cycle the meal count runs on, read off the same sentence as the count:
#: "…remaining in your meal plan for the current **week**". Ordered
#: most-specific-first, and matched rather than assumed — weekly plans and
#: per-term block plans both exist, and saying "this week" to a block-plan
#: holder would misstate when the number resets. An unrecognised wording gives
#: ``""``, which the UI renders as no qualifier at all.
PERIOD_MARKERS = (
    ("week", ("current week", "this week", "per week", "week")),
    ("term", ("current term", "current semester", "this term", "semester", "term")),
)


class MealsProvider(Provider[MealPlan]):
    """Meal plan balances for the signed-in student."""

    path = MEALS_PATH
    label = "Meal plan"

    def parse(self, response: Response) -> MealPlan:
        return parse_meals(response.body)


def parse_meals(body: str) -> MealPlan:
    """Read the three balances out of the meal-plan page.

    Raises :class:`ParseError` only if the page has no recognisable meal-plan
    section at all. A *missing individual figure* is not an error — it is
    reported as ``None`` and rendered as "not reported", because a student may
    legitimately have no meal plan, and a confident ``0`` would be worse than
    an honest blank.
    """
    if not (body or "").strip():
        raise ParseError("empty response body for the meal-plan page")

    tree = parse_html(body)

    paragraphs = [(_text(p), p) for p in tree.find_all("p")]
    if not paragraphs:
        raise ParseError("no paragraphs in the meal-plan page — it has changed shape")

    meals = _find_int(paragraphs, MEALS_MARKERS)
    dining = _find_money(paragraphs, DINING_MARKERS, exclude=FLEX_MARKERS)
    flex = _find_money(paragraphs, FLEX_MARKERS)

    plan = MealPlan(
        meals_remaining=meals,
        dining_dollars=dining,
        flex_dollars=flex,
        student_name=_student_name(tree),
        prox_card_id=_find_text(paragraphs, PROX_MARKERS),
        period=_find_period(paragraphs),
    )

    if not plan.has_any:
        raise ParseError(
            "found no meal-plan figures on the page. Either the layout changed "
            "or this account has no meal plan; recapture with "
            "`scripts/discover meals` to tell the two apart."
        )

    log.debug(
        "meals: %s meals, dining=%s, flex=%s",
        plan.meals_remaining, plan.dining_dollars, plan.flex_dollars,
    )
    return plan


# ---------------------------------------------------------------------------

def _matching(paragraphs, markers, exclude=()):
    """Paragraphs whose text contains one of ``markers`` and none of ``exclude``.

    ``exclude`` exists because "dining dollars" and "voluntary flex dollars"
    both end in "dollars" and both sit in a "You have $N remaining in…"
    sentence; without it a loosened marker could match the wrong line.
    """
    for text, node in paragraphs:
        lowered = text.lower()
        if any(m in lowered for m in markers) and not any(x in lowered for x in exclude):
            yield text, node


def _first_strong(node) -> str:
    """Text of the FIRST ``<strong>`` in a paragraph.

    Load-bearing: the Voluntary Flex paragraph's second ``<strong>`` is the
    words "do not", so anything less specific than "the first one" will
    eventually return that instead of an amount.
    """
    for strong in node.find_all("strong", "b"):
        text = _text(strong)
        if text:
            return text
    return ""


def _find_int(paragraphs, markers) -> int | None:
    for text, node in _matching(paragraphs, markers):
        value = _as_int(_first_strong(node))
        if value is None:
            value = _as_int(text)          # fall back to the sentence itself
        if value is not None:
            return value
    return None


def _find_money(paragraphs, markers, exclude=()) -> float | None:
    for text, node in _matching(paragraphs, markers, exclude):
        value = _as_money(_first_strong(node))
        if value is None:
            value = _as_money(text)
        if value is not None:
            return value
    return None


def _find_period(paragraphs) -> str:
    """``"week"`` / ``"term"`` / ``""`` — which cycle the meal count runs on.

    Only the meals sentence is examined. The dollar paragraphs also say "the
    end of the current term", and reading the period off the page as a whole
    would therefore call every plan a term plan.
    """
    for text, _node in _matching(paragraphs, MEALS_MARKERS):
        lowered = text.lower()
        for period, markers in PERIOD_MARKERS:
            if any(m in lowered for m in markers):
                return period
    return ""


def _find_text(paragraphs, markers) -> str:
    for _text_, node in _matching(paragraphs, markers):
        value = _first_strong(node)
        if value:
            return value
    return ""


def _student_name(tree: Element) -> str:
    """The ``<h5>`` inside the meal-plan fieldset, if it is there."""
    for fieldset in tree.find_all("fieldset"):
        for node in fieldset.find_all("h5"):
            text = _text(node)
            if text:
                return text
    return ""


def _as_int(text: str) -> int | None:
    match = re.search(r"\b(\d{1,4})\b", text or "")
    return int(match.group(1)) if match else None


def _as_money(text: str) -> float | None:
    """Parse ``$112.08`` / ``1,234.56`` / ``$0.00``.

    Requires a currency marker or a decimal part, so the ``19`` from the meals
    sentence can never be read as a dollar amount.
    """
    if not text:
        return None
    match = re.search(r"\$\s*(-?[\d,]+(?:\.\d{1,2})?)|(-?[\d,]+\.\d{2})\b", text)
    if not match:
        return None
    raw = match.group(1) or match.group(2)
    try:
        return float(raw.replace(",", ""))
    except ValueError:
        return None


def _text(node: Element) -> str:
    return re.sub(r"\s+", " ", node.text_content()).strip()
