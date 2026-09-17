"""Dining menus — "what's at Home Cooking today".

**This one is fully known.** Unlike the chapel provider, nothing here is a
guess: the endpoint, its parameters and its response shape were all verified
against the live service on 2026-09-16, and a real capture is committed at
``tests/fixtures/diningdata_cedarville_edu_api_menus.json``.

.. rubric:: The endpoint

    GET https://diningdata.cedarville.edu/api/menus?days=N

Unauthenticated — `diningdata.cedarville.edu`'s own page fetches it with
``credentials: "omit"``, so there is no session to have and nothing to log into.
That is why :class:`~mycu.core.transport.TransportRouter` sends this origin to
:class:`~mycu.core.transport.HttpTransport` rather than through the WebView.

Response shape, verified::

    {
      "2026-09-16": [
        {
          "venue": "Home Cooking",
          "meal":  "Breakfast",
          "slot":  "breakfast",
          "items": [
            {"name": "Chorizo Sausage Patties", "allergens": []},
            {"name": "Shredded Cheese",
             "allergens": [{"url": "…/hasDairy.png", "alt": "dairy"}]}
          ]
        },
        …
      ],
      "2026-09-17": [ … ]
    }

About twenty blocks per day. ``venue`` is the station — observed values include
Home Cooking, Grille, Italian, Habanero, SubZone, Salad & Soup, Bake Shoppe,
Allergen Aware, Garden Bites, Power Bar, Self Cook, Breakfast All Day, Self
Cook. ``meal`` is ``"Breakfast"``/``"Lunch"``/``"Dinner"`` for stations tied to
a sitting, and ``null`` for all-day stations, which carry ``slot: "anytime"``
instead.

``days=N`` was tested at 1, 7 and 14 and returns exactly N dates starting
today. There is also a ``refresh=1`` parameter, which the site's own script
uses hourly; this provider never sends it, because it appears to force an
upstream refetch from Pioneer College Caterers (``my.pcconline.com``) and there
is no reason for a personal app to make someone else's server work harder.
"""

from __future__ import annotations

import logging
from datetime import date, datetime
from typing import Any

from ..errors import ParseError
from ..models import HOME_COOKING, DayMenu, MenuBlock, MenuItem
from ..transport import DINING_BASE, Response
from .base import Provider

log = logging.getLogger(__name__)

#: Path including the origin — this provider does not live on Self-Service, and
#: `TransportRouter` reads the origin to pick a transport.
DINING_PATH = f"{DINING_BASE}/api/menus"

#: How far ahead to ask for. One week: enough for "what's for dinner on Friday"
#: without pulling a quarter-megabyte of JSON onto a phone.
DEFAULT_DAYS = 7


class DiningProvider(Provider[tuple[DayMenu, ...]]):
    """Menus for the next ``days`` days, oldest first."""

    label = "Dining"

    def __init__(self, transport, days: int = DEFAULT_DAYS) -> None:
        super().__init__(transport)
        self.days = max(1, int(days))

    @property
    def path(self) -> str:  # type: ignore[override]
        return f"{DINING_PATH}?days={self.days}"

    def parse(self, response: Response) -> tuple[DayMenu, ...]:
        return parse_menus(response.json())


def parse_menus(payload: Any) -> tuple[DayMenu, ...]:
    """Turn the API's ``{date: [block]}`` mapping into :class:`DayMenu` objects.

    Sorted by date, because dict ordering is a JSON serialisation detail and
    not something a UI should depend on.

    Individual malformed blocks are skipped with a warning rather than taken as
    fatal: a menu missing one station is still a useful menu, and this endpoint
    is ultimately reformatting someone else's data.
    """
    if not isinstance(payload, dict):
        raise ParseError(
            f"expected a {{date: [blocks]}} object from the dining API, "
            f"got {type(payload).__name__}"
        )

    days: list[DayMenu] = []
    for raw_date in sorted(payload):
        on = _parse_iso_date(raw_date)
        if on is None:
            log.warning("dining: skipping unparseable date key %r", raw_date)
            continue

        raw_blocks = payload[raw_date]
        if not isinstance(raw_blocks, list):
            log.warning("dining: %s did not map to a list of blocks", raw_date)
            continue

        blocks = []
        for raw_block in raw_blocks:
            block = _parse_block(raw_block)
            if block is not None:
                blocks.append(block)

        days.append(DayMenu(on=on, blocks=tuple(blocks)))

    if not days:
        raise ParseError("the dining API returned no usable days")

    return tuple(days)


def _parse_iso_date(text: str) -> date | None:
    try:
        return datetime.strptime(str(text), "%Y-%m-%d").date()
    except (ValueError, TypeError):
        return None


def _parse_block(raw: Any) -> MenuBlock | None:
    if not isinstance(raw, dict):
        return None

    venue = _clean(raw.get("venue"))
    if not venue:
        return None

    return MenuBlock(
        venue=venue,
        # `meal` is null for all-day stations; normalise to "" so the model
        # never has to think about None, and `sort_key` puts them last.
        meal=_clean(raw.get("meal")),
        slot=_clean(raw.get("slot")),
        items=tuple(
            item for item in (_parse_item(i) for i in raw.get("items") or []) if item
        ),
    )


def _parse_item(raw: Any) -> MenuItem | None:
    if not isinstance(raw, dict):
        return None

    name = _clean(raw.get("name"))
    if not name:
        return None

    # The live API sends `allergens: [{url, alt}]`. The site's own menu.js still
    # reads `tags`, which the API no longer returns — its renderer is out of
    # date with its own backend. Accept both: if `tags` ever comes back, it
    # costs one line to keep working.
    raw_allergens = raw.get("allergens")
    if raw_allergens is None:
        raw_allergens = raw.get("tags") or []

    allergens: list[str] = []
    for entry in raw_allergens:
        if isinstance(entry, dict):
            label = _clean(entry.get("alt") or entry.get("name"))
        else:
            label = _clean(entry)
        if label and label not in allergens:
            allergens.append(label)

    return MenuItem(name=name, allergens=tuple(allergens))


def _clean(value: Any) -> str:
    """Normalise whitespace; ``None`` becomes ``""``.

    Worth doing: the upstream data has doubled spaces in real dish names
    ("Eggs with Peppers and  Onion" is in the committed fixture, verbatim).
    """
    if value is None:
        return ""
    return " ".join(str(value).split())


def home_cooking_for(days: "tuple[DayMenu, ...]", on: date | None = None) -> tuple[MenuBlock, ...]:
    """Home Cooking's breakfast, lunch and dinner for one date.

    Defaults to today. Returns an empty tuple if that date is not in the
    payload — which happens legitimately: the API only serves forward from
    today, so asking about yesterday gets you nothing.
    """
    target = on or date.today()
    for day in days:
        if day.on == target:
            return day.for_venue(HOME_COOKING)
    return ()
