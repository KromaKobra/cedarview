"""Upcoming chapels — who is speaking next.

**Verified, unauthenticated, and found rather than guessed.**

`cedarville.edu/chapel` renders its "Upcoming" tab client-side, so the schedule
is nowhere in the served HTML — which is why searching the page for it failed.
Running ``scripts/discover chapel-schedule`` read the page's own resource timing
log and turned up the API it actually calls::

    GET https://mediaserve.cedarville.edu/ChapelMedia/api/v2/chapels/upcoming
        ?page=1&count=N

No auth, no cookies, no session. Response, verified 2026-09-17::

    {
      "TotalCount": 51, "Page": 1, "RequestedCount": 20, "RetrievedCount": 20,
      "Items": [
        {"Id": "ICiRTos5BUiwi_xQP8LHyg",
         "Title": "Garrett Higbee",
         "Date": "2026-09-17T14:00:00Z",
         "Description": "…",
         "YouTubeId": "ySIBhMll7k0",
         "PreviewUrl": "…",
         "Speakers": ["Garrett Higbee"],
         "WillLiveStream": true}
      ]
    }

``count`` is capped at 30 by the server (``count=100`` answers with
``RequestedCount: 30``), so the whole schedule takes more than one page:
on 2026-09-22 ``TotalCount`` was 47, page 1 held 30, page 2 held 17 and page 3
was empty. :func:`fetch_schedule` walks the pages.

Sibling endpoints on the same API, should you want them later:
``/chapels/recent``, ``/chapels/popular``, ``/chapel/live``. ``/chapel/live``
is where :data:`CHAPEL_LENGTH` comes from — it reports the current or next
chapel with ``StartDate``/``EndDate`` (14:00Z–14:45Z), which the upcoming feed
does not.

.. rubric:: Two things the real data teaches

* ``Speakers`` is **often empty**. "Worship Chapel", "SGA" and similar are real
  scheduled chapels with no named speaker. Anything that assumes ``Speakers[0]``
  exists will crash on the second item in the live feed.
* ``Title`` is frequently the speaker's name verbatim, so rendering
  "title — speaker" gives you "Garrett Kell — Garrett Kell".

Both are handled by :class:`~mycu.core.models.UpcomingChapel`.

.. rubric:: Times

``Date`` is ISO 8601 in UTC (``14:00:00Z`` = 10:00 Eastern). It is parsed to an
aware datetime and converted to local time for display, rather than assuming
the phone is in Ohio.
"""

from __future__ import annotations

import logging
from datetime import datetime, timedelta, timezone
from typing import Any

from ..errors import ParseError
from ..models import UpcomingChapel
from ..transport import Response
from .base import Provider

log = logging.getLogger(__name__)

#: The chapel media service. Unauthenticated, so it is routed to HttpTransport.
CHAPEL_MEDIA_BASE = "https://mediaserve.cedarville.edu"

UPCOMING_PATH = f"{CHAPEL_MEDIA_BASE}/ChapelMedia/api/v2/chapels/upcoming"

#: How many to ask for. The feed had 51 entries when checked — a full semester.
#: Ten is a fortnight or so, which is as far ahead as anyone plans chapel.
DEFAULT_COUNT = 10

#: The most the server will return per page, whatever ``count`` asks for.
PAGE_SIZE = 30

#: A ceiling on pages walked by :func:`fetch_schedule` — five pages is 150
#: chapels, more than a whole term. Only there so a feed that never returns a
#: short page cannot keep the app fetching forever.
MAX_PAGES = 5

#: How long a chapel lasts. The upcoming feed carries only a start time;
#: ``/chapel/live`` gives both ends (14:00Z–14:45Z on 2026-09-23), and this is
#: that gap. It decides when a chapel is "now" and when it is over.
CHAPEL_LENGTH = timedelta(minutes=45)


class ChapelScheduleProvider(Provider[tuple[UpcomingChapel, ...]]):
    """One page of upcoming chapels, soonest first."""

    label = "Chapel schedule"

    def __init__(self, transport, count: int = DEFAULT_COUNT, page: int = 1) -> None:
        super().__init__(transport)
        self.count = max(1, int(count))
        self.page = max(1, int(page))

    @property
    def path(self) -> str:  # type: ignore[override]
        return f"{UPCOMING_PATH}?page={self.page}&count={self.count}"

    def parse(self, response: Response) -> tuple[UpcomingChapel, ...]:
        return parse_upcoming(response.json())


def parse_upcoming(payload: Any) -> tuple[UpcomingChapel, ...]:
    """Turn the API envelope into :class:`UpcomingChapel` objects, soonest first.

    An empty ``Items`` list is **not** an error — over the summer, or after the
    last chapel of a term, there genuinely is nothing upcoming. Returning an
    empty tuple lets the UI say so honestly instead of showing a failure.
    """
    if not isinstance(payload, dict):
        raise ParseError(
            f"expected an object from the chapel schedule API, got {type(payload).__name__}"
        )

    items = payload.get("Items")
    if items is None:
        raise ParseError(f"no 'Items' key in the response; keys were {sorted(payload)}")
    if not isinstance(items, list):
        raise ParseError(f"'Items' was {type(items).__name__}, expected a list")

    chapels = [c for c in (_parse_item(i) for i in items) if c is not None]
    return _soonest_first(chapels)


def _soonest_first(chapels) -> tuple[UpcomingChapel, ...]:
    """Sort by time, putting undated entries last rather than crashing on the
    comparison. Upstream order has been chronological so far, but that is not
    promised anywhere."""
    return tuple(sorted(
        chapels,
        key=lambda c: (c.starts_at is None, c.starts_at or datetime.max.replace(tzinfo=timezone.utc)),
    ))


def fetch_schedule(
    transport, page_size: int = PAGE_SIZE, max_pages: int = MAX_PAGES,
) -> tuple[UpcomingChapel, ...]:
    """Every upcoming chapel in the feed, soonest first, across pages.

    Stops at the first page shorter than ``page_size``, which is how the feed
    says it has run out — two requests for a typical term. Duplicates are
    dropped, in case the feed shifts between page requests (a chapel starting
    between the two would move every later one up a slot).
    """
    seen: set[UpcomingChapel] = set()
    chapels: list[UpcomingChapel] = []
    for page in range(1, max_pages + 1):
        batch = ChapelScheduleProvider(transport, count=page_size, page=page).fetch()
        for chapel in batch:
            if chapel not in seen:
                seen.add(chapel)
                chapels.append(chapel)
        if len(batch) < page_size:
            break
    else:
        log.warning("chapel schedule: stopped after %d pages", max_pages)
    return _soonest_first(chapels)


def _parse_item(raw: Any) -> UpcomingChapel | None:
    if not isinstance(raw, dict):
        return None

    speakers = tuple(
        s.strip() for s in (raw.get("Speakers") or []) if isinstance(s, str) and s.strip()
    )

    return UpcomingChapel(
        starts_at=_parse_utc(raw.get("Date")),
        title=(raw.get("Title") or "").strip(),
        speakers=speakers,
        description=(raw.get("Description") or "").strip(),
        will_livestream=bool(raw.get("WillLiveStream")),
    )


def _parse_utc(text: Any) -> datetime | None:
    """Parse ``2026-09-17T14:00:00Z`` into an aware, local-time datetime.

    Returns ``None`` rather than raising: one unreadable date should cost that
    one entry, not the whole schedule.
    """
    if not text:
        return None
    try:
        parsed = datetime.fromisoformat(str(text).replace("Z", "+00:00"))
    except ValueError:
        log.debug("chapel schedule: unparseable date %r", text)
        return None

    if parsed.tzinfo is None:
        # The API has always sent an explicit Z. If that ever changes, assume
        # UTC rather than local — guessing local would shift every time by the
        # viewer's offset.
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed.astimezone()


def next_chapel(chapels: "tuple[UpcomingChapel, ...]") -> UpcomingChapel | None:
    """The soonest chapel that has not already started.

    The feed keeps today's chapel listed after it has begun, so "next" has to
    mean "still in the future" rather than "first in the list".
    """
    now = datetime.now().astimezone()
    for chapel in chapels:
        if chapel.starts_at is not None and chapel.starts_at > now:
            return chapel
    return None


def is_over(chapel: UpcomingChapel, now: datetime) -> bool:
    """Whether ``chapel`` has finished. Undated chapels are never over."""
    return chapel.starts_at is not None and chapel.starts_at + CHAPEL_LENGTH <= now


def is_happening(chapel: UpcomingChapel, now: datetime) -> bool:
    """Whether ``chapel`` has started and not yet finished."""
    return chapel.starts_at is not None and chapel.starts_at <= now and not is_over(chapel, now)
