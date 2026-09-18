"""Domain dataclasses.

Deliberately not a mirror of whatever Ellucian sends. These are the shapes the
QML layer binds to; the provider's job is to translate. When Cedarville renames
a column, only the provider changes.

.. warning::
   Field names here are *ours* and are stable. The field names in the upstream
   payload are **not yet known** — see ``docs/discovery.md`` (milestone M0).
   Where a model field maps to a guessed upstream key, the provider marks it
   with ``# M0:`` so every unconfirmed assumption is greppable:

       grep -rn 'M0:' mycu/
"""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import date, datetime
from enum import Enum


@dataclass(frozen=True, slots=True)
class AllowanceLine:
    """One component of the skip allowance.

    Real example: a base allowance of 17 plus a "Manual Arrangement" of 1,
    summing to the reported total of 18.
    """

    reason: str
    count: int
    description: str = ""


@dataclass(frozen=True, slots=True)
class ChapelLedgerEntry:
    """One line of the chapel-skip ledger.

    This is a **ledger, not an attendance register** — a distinction that
    matters. Entries are not "you were absent on this date"; they are movements
    against the skip balance:

    * ``EntryType: "Chapel Skip"``, ``Count: 1`` — an absence was recorded.
    * ``EntryType: "Manual Adjustment"``, ``Count: -1`` — a skip was given back
      (in the captured data: "Had ID replaced").

    So ``on`` is genuinely ``None`` for adjustments: they are not tied to a
    chapel. :attr:`when` exists so the UI always has something true to show.
    """

    on: datetime | None
    count: int
    entry_type: str = ""
    reason: str = ""
    created_at: datetime | None = None
    can_remove: bool = False

    @property
    def is_skip(self) -> bool:
        return self.count > 0

    @property
    def when(self) -> str:
        """A date to show, falling back to the reason for undated adjustments."""
        if self.on is not None:
            return f"{self.on.strftime('%a %b')} {self.on.day}, {self.on.year}"
        return self.reason or self.entry_type or "—"


@dataclass(frozen=True, slots=True)
class ChapelSummary:
    """Everything the chapel screen needs.

    .. important::
       ``used``, ``total`` and ``remaining`` are **reported by Cedarville and
       never computed here.** That is not a stylistic preference — deriving them
       gives the wrong answer. In the captured data the server reports
       ``SkipsUsed: 2`` while the ledger's counts sum to ``1``, because the
       ``-1`` manual adjustment is *also* represented as a ``+1`` line in the
       allowance breakdown (17 base + 1 arrangement = 18 total; 18 - 2 = 16
       remaining). Both halves are consistent on the server's terms and
       inconsistent on ours. The school's arithmetic is the one that counts.
    """

    used: int | None = None
    total: int | None = None
    remaining: int | None = None

    term: str = ""
    term_name: str = ""
    student_name: str = ""
    student_id: str = ""

    allowance: tuple[AllowanceLine, ...] = ()
    requirement_reasons: tuple[str, ...] = ()
    is_required_to_attend: bool = True
    is_in_good_standing: bool = True
    status: str = ""

    entries: tuple[ChapelLedgerEntry, ...] = ()
    fines: tuple[dict, ...] = ()

    @property
    def label(self) -> str:
        """The nicer of the two term spellings — "Fall Semester 2026" over "2026FA"."""
        return self.term_name or self.term


@dataclass(slots=True)
class CachedPayload:
    """A response body plus when we got it.

    The app caches and refreshes on demand rather than polling — this is one
    student reading their own record, and it should behave like it.
    """

    path: str
    body: str
    fetched_at: float
    final_url: str = ""
    headers: dict[str, str] = field(default_factory=dict)


# ---------------------------------------------------------------------------
# Dining
#
# Unlike the chapel models, these are NOT guesses. They mirror the real,
# verified response of https://diningdata.cedarville.edu/api/menus?days=N,
# captured 2026-09-16 and committed to tests/fixtures/.
# ---------------------------------------------------------------------------

#: The dining hall station this app is actually about. The API calls it a
#: "venue"; The Commons has about a dozen (Grille, Italian, Habanero, SubZone…)
#: and this is the one worth a screen of its own.
HOME_COOKING = "Home Cooking"

#: ``slot`` values, in the order the sittings happen. Anything else — in
#: practice ``"anytime"``, for stations that run all day — sorts last.
#:
#: **Order on ``slot``, never on ``meal``.** Checked against the real payload:
#: ``slot`` is always one of these four, but ``meal`` is a free-text label that
#: is sometimes the sitting (``"Breakfast"``), sometimes ``null`` (Grille,
#: Italian, SubZone…), and sometimes a sub-station name (``"Deli"`` under
#: Allergen Aware, ``"yogurt bar"`` under Breakfast All Day). Keying the sort on
#: ``meal`` put "yogurt bar" — a breakfast item — after dinner.
SLOT_ORDER = ("breakfast", "lunch", "dinner")

#: Pretty names for the slots, for when ``meal`` is null and we still want a
#: heading.
SLOT_LABELS = {
    "breakfast": "Breakfast",
    "lunch": "Lunch",
    "dinner": "Dinner",
    "anytime": "All day",
}


@dataclass(frozen=True, slots=True)
class MenuItem:
    """One dish.

    ``allergens`` comes back from the API as a list of ``{url, alt}`` icon
    objects; only the ``alt`` text ("dairy", "gluten", "egg", "soy"…) is worth
    keeping, so the icon URLs are dropped at parse time.
    """

    name: str
    allergens: tuple[str, ...] = ()

    @property
    def allergen_text(self) -> str:
        return ", ".join(self.allergens)


@dataclass(frozen=True, slots=True)
class MenuBlock:
    """One station, at one sitting, on one day.

    ``slot`` is the machine-readable sitting (``breakfast``/``lunch``/
    ``dinner``/``anytime``); ``meal`` is the upstream's free-text label for the
    block and may be empty. Use :attr:`heading` for display.
    """

    venue: str
    meal: str
    slot: str
    items: tuple[MenuItem, ...] = ()

    @property
    def sort_key(self) -> int:
        """Breakfast, then lunch, then dinner; all-day stations last.

        The API does not return blocks in chronological order, and a menu
        listing dinner before breakfast reads as a bug even when every item on
        it is correct. Keys on :attr:`slot` — see :data:`SLOT_ORDER` for why
        ``meal`` is the wrong field for this.
        """
        try:
            return SLOT_ORDER.index(self.slot.casefold())
        except ValueError:
            return len(SLOT_ORDER)

    @property
    def heading(self) -> str:
        """What to show above this block.

        Prefers the upstream's own label, because it is sometimes more specific
        than the slot ("Deli", "yogurt bar"), and falls back to the slot when it
        is missing — which is the common case for all-day stations.
        """
        return self.meal or SLOT_LABELS.get(self.slot.casefold(), self.slot.title())


@dataclass(frozen=True, slots=True)
class DayMenu:
    """Every block served on one date."""

    on: date
    blocks: tuple[MenuBlock, ...] = ()

    def for_venue(self, venue: str = HOME_COOKING) -> tuple[MenuBlock, ...]:
        """This day's blocks for one station, in meal order.

        >>> day.for_venue()            # doctest: +SKIP
        (Breakfast block, Lunch block, Dinner block)
        """
        matching = [b for b in self.blocks if b.venue.casefold() == venue.casefold()]
        return tuple(sorted(matching, key=lambda b: b.sort_key))

    @property
    def venues(self) -> tuple[str, ...]:
        """Every station serving that day, deduplicated, in first-seen order."""
        seen: dict[str, None] = {}
        for block in self.blocks:
            seen.setdefault(block.venue, None)
        return tuple(seen)


# ---------------------------------------------------------------------------
# Chapel schedule
#
# Also not guesses: the shape below was read off the live v2 API, which was
# found by watching cedarville.edu/chapel's own network traffic
# (`scripts/discover chapel-schedule`). Real capture in tests/fixtures/.
# ---------------------------------------------------------------------------

@dataclass(frozen=True, slots=True)
class UpcomingChapel:
    """A scheduled chapel that has not happened yet.

    ``speakers`` is genuinely often empty — "Worship Chapel", "SGA" and the
    like are real scheduled chapels with no named speaker. :attr:`who` exists so
    the UI never has to render an empty string.
    """

    starts_at: datetime | None
    title: str = ""
    speakers: tuple[str, ...] = ()
    description: str = ""
    will_livestream: bool = False

    @property
    def who(self) -> str:
        """Who is speaking, or the event's own name when nobody is named."""
        if self.speakers:
            return ", ".join(self.speakers)
        return self.title or "Chapel"

    @property
    def is_same_as_title(self) -> bool:
        """Whether the title adds nothing beyond what :attr:`who` already says.

        The API commonly sets ``Title`` to the speaker's name verbatim, and
        showing "Garrett Kell — Garrett Kell" looks like a bug.

        Compared against :attr:`who`, not against the speaker list, because
        ``who`` is what the UI actually renders. With the list it read as
        "does the title repeat the speakers?", which is False whenever there
        are no speakers — and in exactly that case ``who`` has already fallen
        back to the title. Seen on the phone: an unnamed "Worship Chapel"
        rendered as "Worship Chapel" above "Worship Chapel".
        """
        return self.title.strip().casefold() == self.who.strip().casefold()

    @property
    def on(self) -> date | None:
        return self.starts_at.date() if self.starts_at else None


# ---------------------------------------------------------------------------
# Meal plan
#
# Verified against the real page (selfservice.cedarville.edu/Cedarinfo/Meals,
# 2026-09-17). Server-rendered: no table, no JSON — the numbers are prose in
# <strong> tags. Real capture (trimmed, scrubbed) in tests/fixtures/.
# ---------------------------------------------------------------------------

@dataclass(frozen=True, slots=True)
class MealPlan:
    """The three balances the meal-plan page reports.

    .. important::
       **There are two different dollar balances**, and conflating them would
       misreport money:

       * :attr:`dining_dollars` — "Meal Plan Dining Dollars". Part of the meal
         plan, and they **expire at the end of the term**.
       * :attr:`flex_dollars` — "purchased Voluntary Flex Dollars". Bought
         separately, and they **do not expire**.

       "Flex dollars" colloquially often means the first one, but on this page
       it is unambiguously the second. Both are surfaced, each labelled with its
       own expiry, rather than picking one and hoping.

    Every field is optional: a student with no meal plan, or a page that changes
    shape, must render as "not reported" rather than as a confident zero.
    """

    meals_remaining: int | None = None
    dining_dollars: float | None = None
    flex_dollars: float | None = None
    student_name: str = ""
    prox_card_id: str = ""

    #: Which cycle :attr:`meals_remaining` counts down — ``"week"`` or
    #: ``"term"``, and ``""`` when the page did not say. Read off the sentence
    #: ("…for the current week"), never assumed: block plans are per-term and
    #: telling a term-plan holder their meals reset on Sunday would be wrong.
    period: str = ""

    #: How long :attr:`meals_remaining` lasts, in words — "this week" /
    #: "this term" / "" when unknown. Here rather than in the QML because
    #: whether the figure is weekly at all is a fact about the data.
    @property
    def period_text(self) -> str:
        return f"this {self.period}" if self.period else ""

    #: "Weekly meal plan" / "Semester meal plan" / "" — the closest thing to a
    #: plan *name* the page supports. The actual plan name ("14 Meals per week")
    #: is not on the meal-plan page; see docs/data-sources.md.
    @property
    def plan_description(self) -> str:
        return {"week": "Weekly meal plan", "term": "Semester meal plan"}.get(
            self.period, ""
        )

    @property
    def has_any(self) -> bool:
        return any(
            v is not None
            for v in (self.meals_remaining, self.dining_dollars, self.flex_dollars)
        )

    @staticmethod
    def money(value: float | None) -> str:
        """``112.08`` -> ``"$112.08"``; ``None`` -> ``""`` (never ``"$0.00"``)."""
        return "" if value is None else f"${value:,.2f}"
