"""Upcoming chapels, against a REAL captured response.

Fixture: `tests/fixtures/mediaserve_cedarville_edu_chapelmedia_api_v2_chapels_upcoming.json`,
a verbatim capture of the live v2 API taken 2026-09-17. No personal data — this
is the public chapel schedule, and the endpoint needs no authentication.

That endpoint was not guessed. `cedarville.edu/chapel` renders its Upcoming tab
client-side, so the schedule is absent from the served HTML;
`scripts/discover chapel-schedule` read the page's own resource-timing log and
found the API it calls.
"""

from __future__ import annotations

import json
from datetime import datetime, timedelta, timezone
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

import pytest

from mycu.core.errors import ParseError
from mycu.core.models import UpcomingChapel
from mycu.core.providers.chapel_schedule import (
    CHAPEL_LENGTH,
    UPCOMING_PATH,
    ChapelScheduleProvider,
    fetch_schedule,
    is_happening,
    is_over,
    next_chapel,
    parse_upcoming,
)
from mycu.core.transport import FixtureTransport, Response

FIXTURE = "mediaserve_cedarville_edu_chapelmedia_api_v2_chapels_upcoming.json"


@pytest.fixture
def upcoming(fixtures_dir: Path) -> tuple[UpcomingChapel, ...]:
    return parse_upcoming(json.loads((fixtures_dir / FIXTURE).read_text()))


# ---------------------------------------------------------------------------
# Shape
# ---------------------------------------------------------------------------

def test_the_capture_has_a_full_page(upcoming) -> None:
    assert len(upcoming) == 20


def test_the_first_entry_is_the_real_one(upcoming) -> None:
    first = upcoming[0]
    assert first.speakers == ("Garrett Higbee",)
    assert first.will_livestream is True
    assert first.starts_at is not None
    # 14:00Z is 10:00 Eastern; asserted in UTC so the test does not depend on
    # the machine's timezone.
    assert first.starts_at.astimezone(timezone.utc).isoformat() == "2026-09-17T14:00:00+00:00"


def test_entries_are_sorted_soonest_first(upcoming) -> None:
    times = [c.starts_at for c in upcoming if c.starts_at]
    assert times == sorted(times)


def test_dates_are_timezone_aware(upcoming) -> None:
    # A naive datetime here would silently shift every chapel by the viewer's
    # UTC offset.
    assert all(c.starts_at.tzinfo is not None for c in upcoming if c.starts_at)


# ---------------------------------------------------------------------------
# The two things the real data teaches
# ---------------------------------------------------------------------------

def test_some_chapels_genuinely_have_no_speaker(upcoming) -> None:
    """"Worship Chapel" and "SGA" are real entries with `Speakers: []`.

    Anything that assumes `Speakers[0]` exists crashes on the *second* item in
    the live feed.
    """
    speakerless = [c for c in upcoming if not c.speakers]
    assert speakerless, "the capture should contain speakerless chapels"
    assert any(c.title == "Worship Chapel" for c in speakerless)


def test_who_falls_back_to_the_event_name(upcoming) -> None:
    worship = next(c for c in upcoming if c.title == "Worship Chapel")
    assert worship.speakers == ()
    assert worship.who == "Worship Chapel"


def test_who_never_returns_an_empty_string() -> None:
    assert UpcomingChapel(starts_at=None).who == "Chapel"


def test_title_duplicating_the_speaker_is_detectable(upcoming) -> None:
    """The API sets Title to the speaker's name, so "X — X" must be avoidable."""
    higbee = upcoming[0]
    assert higbee.title == "Garrett Higbee"
    assert higbee.is_same_as_title is True


def test_an_unnamed_chapel_does_not_print_its_own_name_twice(upcoming) -> None:
    """Regression, seen on the phone: "Worship Chapel" above "Worship Chapel".

    This assertion used to read ``is_same_as_title is False`` and was wrong —
    it encoded the property's old question, "does the title repeat the
    *speakers*?", which is trivially False when there are no speakers. But a
    chapel with no named speaker is precisely when ``who`` falls back to the
    title, so the screen rendered it twice. The question that matters is "does
    the title repeat what we are already showing?".
    """
    worship = next(c for c in upcoming if c.title == "Worship Chapel")
    assert worship.speakers == ()
    assert worship.who == "Worship Chapel"
    assert worship.is_same_as_title is True


# ---------------------------------------------------------------------------
# next_chapel
# ---------------------------------------------------------------------------

def _at(hours: float) -> datetime:
    return datetime.now().astimezone() + timedelta(hours=hours)


def test_next_chapel_skips_one_that_already_started() -> None:
    """The feed keeps today's chapel listed after it has begun."""
    past = UpcomingChapel(starts_at=_at(-2), title="Already happened")
    future = UpcomingChapel(starts_at=_at(2), title="Coming up")

    assert next_chapel((past, future)) is future


def test_next_chapel_is_none_when_nothing_is_upcoming() -> None:
    # Legitimate over the summer; must not be an error.
    assert next_chapel(()) is None
    assert next_chapel((UpcomingChapel(starts_at=_at(-1)),)) is None


def test_next_chapel_ignores_undated_entries() -> None:
    assert next_chapel((UpcomingChapel(starts_at=None),)) is None


# ---------------------------------------------------------------------------
# Robustness
# ---------------------------------------------------------------------------

def test_an_empty_schedule_is_not_an_error() -> None:
    assert parse_upcoming({"Items": [], "TotalCount": 0}) == ()


def test_a_missing_items_key_raises_with_the_keys_it_did_see() -> None:
    with pytest.raises(ParseError, match="Items"):
        parse_upcoming({"TotalCount": 0})


def test_a_non_object_payload_raises() -> None:
    with pytest.raises(ParseError):
        parse_upcoming([{"Title": "x"}])


def test_a_malformed_entry_is_skipped_not_fatal() -> None:
    chapels = parse_upcoming({"Items": ["nonsense", {"Title": "Real", "Date": "2026-09-17T14:00:00Z"}]})
    assert len(chapels) == 1
    assert chapels[0].title == "Real"


def test_an_unparseable_date_costs_only_that_entry() -> None:
    chapels = parse_upcoming({"Items": [
        {"Title": "Bad date", "Date": "next Tuesday-ish"},
        {"Title": "Good", "Date": "2026-09-17T14:00:00Z"},
    ]})
    assert len(chapels) == 2
    # Undated entries sort last rather than crashing the comparison.
    assert chapels[0].title == "Good"
    assert chapels[-1].starts_at is None


def test_a_date_without_a_zone_is_assumed_utc() -> None:
    """Guessing local would shift every time by the viewer's offset."""
    chapels = parse_upcoming({"Items": [{"Date": "2026-09-17T14:00:00"}]})
    assert chapels[0].starts_at.astimezone(timezone.utc).hour == 14


def test_blank_speaker_entries_are_dropped() -> None:
    chapels = parse_upcoming({"Items": [{"Speakers": ["  ", "", "Real Person"]}]})
    assert chapels[0].speakers == ("Real Person",)


# ---------------------------------------------------------------------------
# The provider
# ---------------------------------------------------------------------------

def test_provider_targets_the_media_api() -> None:
    assert ChapelScheduleProvider(None).path.startswith(UPCOMING_PATH)


def test_provider_requests_the_count_it_was_given() -> None:
    assert ChapelScheduleProvider(None, count=5).path.endswith("count=5")


def test_count_is_clamped() -> None:
    assert ChapelScheduleProvider(None, count=0).count == 1


def test_provider_end_to_end_over_the_fixture(fixtures_dir: Path) -> None:
    chapels = ChapelScheduleProvider(FixtureTransport(fixtures_dir)).fetch()
    assert len(chapels) == 20
    assert chapels[0].who == "Garrett Higbee"


def test_provider_asks_for_page_one_unless_told_otherwise() -> None:
    assert "?page=1&" in ChapelScheduleProvider(None).path
    assert "?page=3&" in ChapelScheduleProvider(None, page=3).path
    assert ChapelScheduleProvider(None, page=0).page == 1


# ---------------------------------------------------------------------------
# The whole feed, across pages
# ---------------------------------------------------------------------------

class PagedTransport:
    """Serves ``pages[n-1]`` for ``page=n``; an empty page past the end."""

    def __init__(self, pages: list[list[dict]]) -> None:
        self.pages = pages
        self.asked: list[int] = []

    def get(self, path: str) -> Response:
        page = int(parse_qs(urlsplit(path).query)["page"][0])
        self.asked.append(page)
        items = self.pages[page - 1] if page <= len(self.pages) else []
        return Response(status=200, url=path, body=json.dumps({"Items": items}))


def _item(day: int, title: str = "Speaker") -> dict:
    return {"Date": f"2026-10-{day:02d}T14:00:00Z", "Title": f"{title} {day}",
            "Speakers": [], "WillLiveStream": True}


def test_fetch_schedule_walks_pages_until_a_short_one() -> None:
    transport = PagedTransport([[_item(1), _item(2)], [_item(3)]])
    chapels = fetch_schedule(transport, page_size=2)
    assert [c.title for c in chapels] == ["Speaker 1", "Speaker 2", "Speaker 3"]
    assert transport.asked == [1, 2]


def test_a_full_last_page_costs_one_empty_request() -> None:
    transport = PagedTransport([[_item(1), _item(2)]])
    assert len(fetch_schedule(transport, page_size=2)) == 2
    assert transport.asked == [1, 2]


def test_fetch_schedule_gives_up_after_max_pages() -> None:
    transport = PagedTransport([[_item(d)] for d in range(1, 20)])
    assert len(fetch_schedule(transport, page_size=1, max_pages=3)) == 3
    assert transport.asked == [1, 2, 3]


def test_an_empty_feed_is_an_empty_schedule() -> None:
    assert fetch_schedule(PagedTransport([])) == ()


def test_a_chapel_repeated_across_pages_is_listed_once() -> None:
    """If a chapel starts between the two requests, page 2 shifts up a slot."""
    transport = PagedTransport([[_item(1), _item(2)], [_item(2)]])
    assert [c.title for c in fetch_schedule(transport, page_size=2)] == [
        "Speaker 1", "Speaker 2",
    ]


def test_fetch_schedule_over_the_fixture_is_one_request(fixtures_dir: Path) -> None:
    # The capture is a 20-item page — shorter than 30, so the feed has ended.
    chapels = fetch_schedule(FixtureTransport(fixtures_dir))
    assert len(chapels) == 20


# ---------------------------------------------------------------------------
# Now and over
# ---------------------------------------------------------------------------

START = datetime(2026, 9, 23, 14, 0, tzinfo=timezone.utc)


def test_a_chapel_is_happening_for_its_length_and_then_over() -> None:
    chapel = UpcomingChapel(starts_at=START)
    assert not is_happening(chapel, START - timedelta(minutes=1))
    assert is_happening(chapel, START)
    assert is_happening(chapel, START + CHAPEL_LENGTH - timedelta(seconds=1))
    assert not is_happening(chapel, START + CHAPEL_LENGTH)
    assert is_over(chapel, START + CHAPEL_LENGTH)
    assert not is_over(chapel, START)


def test_an_undated_chapel_is_neither_happening_nor_over() -> None:
    chapel = UpcomingChapel(starts_at=None)
    assert not is_happening(chapel, START)
    assert not is_over(chapel, START)
