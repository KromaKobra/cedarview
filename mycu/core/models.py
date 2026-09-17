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


class AttendanceStatus(str, Enum):
    """How a single chapel session was recorded.

    ``str`` mixin so QML sees a plain string and ``json.dumps`` works without a
    custom encoder.

    The set of values Cedarville actually uses is unconfirmed; :meth:`parse`
    maps anything unrecognised to :attr:`UNKNOWN` rather than raising, so an
    unexpected status shows up in the UI as "unknown" instead of taking the
    whole screen down.
    """

    PRESENT = "present"
    ABSENT = "absent"
    EXCUSED = "excused"
    EXEMPT = "exempt"
    UNKNOWN = "unknown"

    @classmethod
    def parse(cls, raw: str | None) -> "AttendanceStatus":
        """Best-effort map from an upstream string to a status.

        Case- and whitespace-insensitive, and tolerant of the common phrasings
        ("Excused Absence", "A", "Unexcused"). Extend the table once M0 shows
        what is really sent.
        """
        if raw is None:
            return cls.UNKNOWN

        text = raw.strip().lower()
        if not text:
            return cls.UNKNOWN

        # M0: replace this table with the exact vocabulary from the real page.
        table = {
            "p": cls.PRESENT,
            "present": cls.PRESENT,
            "attended": cls.PRESENT,
            "a": cls.ABSENT,
            "absent": cls.ABSENT,
            "unexcused": cls.ABSENT,
            "unexcused absence": cls.ABSENT,
            "skip": cls.ABSENT,
            "e": cls.EXCUSED,
            "excused": cls.EXCUSED,
            "excused absence": cls.EXCUSED,
            "x": cls.EXEMPT,
            "exempt": cls.EXEMPT,
        }
        return table.get(text, cls.UNKNOWN)


@dataclass(frozen=True, slots=True)
class ChapelRecord:
    """One chapel session as it appears on a student's record.

    ``on`` is optional because a server-rendered table may carry a date string
    we cannot confidently parse; in that case :attr:`raw_date` keeps the
    original text so the UI can still show something truthful.
    """

    on: date | None
    status: AttendanceStatus
    raw_date: str = ""
    title: str = ""
    note: str = ""

    @property
    def counts_as_skip(self) -> bool:
        """Whether this session counts against the allowance.

        Only unexcused absences do. Excused and exempt sessions do not, and
        ``UNKNOWN`` deliberately does not — guessing high would make the app lie
        in the scary direction.
        """
        return self.status is AttendanceStatus.ABSENT


@dataclass(frozen=True, slots=True)
class ChapelSummary:
    """Everything the chapel screen needs, in one object.

    ``allowed`` and ``used`` are reported by Cedarville when available rather
    than computed, because the official number is the one that matters — the
    school's arithmetic wins over ours. :meth:`derived` builds a summary from
    records alone for the case where the page does not state the totals.
    """

    records: tuple[ChapelRecord, ...] = ()
    allowed: int | None = None
    used: int | None = None
    term: str = ""
    student_name: str = ""

    @property
    def remaining(self) -> int | None:
        """Skips left, or ``None`` if we do not have both halves of the sum."""
        if self.allowed is None or self.used is None:
            return None
        return self.allowed - self.used

    @classmethod
    def derived(
        cls,
        records: "list[ChapelRecord] | tuple[ChapelRecord, ...]",
        *,
        allowed: int | None = None,
        term: str = "",
        student_name: str = "",
    ) -> "ChapelSummary":
        """Build a summary counting skips ourselves.

        Used when the page lists sessions but does not print a total.
        """
        records = tuple(records)
        return cls(
            records=records,
            allowed=allowed,
            used=sum(1 for r in records if r.counts_as_skip),
            term=term,
            student_name=student_name,
        )


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
        """Whether the title adds nothing beyond the speaker list.

        The API commonly sets ``Title`` to the speaker's name verbatim, and
        showing "Garrett Kell — Garrett Kell" looks like a bug.
        """
        return self.title.strip().casefold() == ", ".join(self.speakers).strip().casefold()

    @property
    def on(self) -> date | None:
        return self.starts_at.date() if self.starts_at else None
