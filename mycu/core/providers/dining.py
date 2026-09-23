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
today. The server caps it at 31: ``days=60``, ``120`` and ``365`` all come back
with 31 dates (about half a megabyte).

``start=YYYY-MM-DD`` moves the first date, in either direction. The site's own
script never sends it, but it was verified on 2026-09-22 against dates from
2025-09-01 to 2026-11-15, all of which returned real menus. It is what lets
the Chucks tab page back to last week or forward past the 31-day cap.

A date with nothing posted — a holiday, a break, or simply too far out — is
not an error. It comes back HTTP 200 with a single placeholder block::

    {"2026-12-25": [{"venue": "No Venues Found", "meal": null,
                     "slot": "anytime", "items": []}]}

which parses like any other block and is dropped by
:meth:`~mycu.core.models.DayMenu.for_venue`, leaving an empty day. There is also a ``refresh=1`` parameter, which the site's own script
uses hourly; this provider never sends it, because it appears to force an
upstream refetch from Pioneer College Caterers (``my.pcconline.com``) and there
is no reason for a personal app to make someone else's server work harder.
"""

from __future__ import annotations

import logging
from datetime import date, datetime, time
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
    """Menus for ``days`` days from ``start`` (default today), oldest first."""

    label = "Dining"

    def __init__(self, transport, days: int = DEFAULT_DAYS, start: date | None = None) -> None:
        super().__init__(transport)
        self.days = max(1, int(days))
        #: First date to ask for; ``None`` leaves it to the server, which means today.
        self.start = start

    @property
    def path(self) -> str:  # type: ignore[override]
        path = f"{DINING_PATH}?days={self.days}"
        if self.start is not None:
            path += f"&start={self.start.isoformat()}"
        return path

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
    payload — which happens legitimately: the payload covers only the window
    that was asked for.
    """
    target = on or date.today()
    for day in days:
        if day.on == target:
            return day.for_venue(HOME_COOKING)
    return ()


#: When each sitting stops being "next".
#:
#: **These are ours, not the API's.** The menu feed carries no serving times at
#: all — only a ``slot`` label — so something has to decide when breakfast stops
#: being the meal you are about to eat. These are the posted Chuck's windows
#: rounded outward, so the answer changes a little late rather than a little
#: early: being told "up next: lunch" while you are still eating breakfast is
#: the more annoying of the two failures.
#:
#: They affect *which menu is shown first* and nothing else. The full day is on
#: the Chucks tab either way, so a wrong guess here costs a tap, not a meal.
SERVING_ENDS = {
    "breakfast": time(10, 30),
    "lunch": time(16, 0),
    "dinner": time(20, 0),
}


def next_meal_block(
    days: "tuple[DayMenu, ...]",
    now: datetime | None = None,
    venue: str = HOME_COOKING,
) -> "tuple[date, MenuBlock] | None":
    """The sitting a reader is most likely about to eat, with its date.

    Walks forward from ``now``: the first sitting today that has not finished
    serving, and failing that the first sitting on the next day in the payload.
    After dinner this means tomorrow's breakfast, which is the honest answer —
    the alternative is showing a menu for a meal that is already over.

    Returns ``None`` when nothing is left in the payload at all. Empty blocks
    are skipped rather than returned: a heading with no dishes under it reads
    as a bug, and the next real sitting is the useful answer.

    ``now`` is injectable because the whole behaviour is a function of the
    clock, and a test that could only be run before 10:30am would be no test.
    """
    moment = now or datetime.now()
    today = moment.date()

    for day in sorted(days, key=lambda d: d.on):
        if day.on < today:
            continue
        for block in day.for_venue(venue):
            # `for_venue` includes all-day stations (slot "anytime"), which are
            # never "next" — they are always on.
            ends = SERVING_ENDS.get(block.slot.casefold())
            if ends is None or not block.items:
                continue
            if day.on > today or moment.time() < ends:
                return day.on, block
    return None
