"""Chapel parsing, against fixtures. No Qt, no network.

.. warning::
   The fixtures these assert against are **synthetic** — see
   ``tests/fixtures/README.md``. Passing here means the parser handles a
   plausible page, not the real one. When you replace the fixtures with real
   captures (milestone M0), the expected values below will need updating, and
   *that is the point of the exercise*: the diff tells you exactly how your
   assumptions were wrong.
"""

from __future__ import annotations

import json
from datetime import date
from pathlib import Path

import pytest

from mycu.core.errors import ParseError, SessionExpired
from mycu.core.models import AttendanceStatus, ChapelRecord, ChapelSummary
from mycu.core.providers.chapel import (
    CHAPEL_PATH,
    ChapelProvider,
    parse_body,
    parse_date,
    parse_html,
    parse_json,
)
from mycu.core.transport import FixtureTransport, Response


# ---------------------------------------------------------------------------
# HTML
# ---------------------------------------------------------------------------

def test_html_extracts_every_row(chapel_html: str) -> None:
    summary = parse_html(chapel_html)
    assert len(summary.records) == 8


def test_html_reads_columns_by_header_not_position(chapel_html: str) -> None:
    first = parse_html(chapel_html).records[0]
    assert first.on == date(2026, 8, 26)
    assert first.status is AttendanceStatus.PRESENT
    assert first.title == "Opening Convocation"


def test_html_maps_status_vocabulary(chapel_html: str) -> None:
    statuses = [r.status for r in parse_html(chapel_html).records]
    assert statuses.count(AttendanceStatus.PRESENT) == 4
    assert statuses.count(AttendanceStatus.ABSENT) == 3
    assert statuses.count(AttendanceStatus.EXCUSED) == 1


def test_html_reads_the_official_totals(chapel_html: str) -> None:
    # The page states "allowed 6" and "used 3"; we must report those rather than
    # our own count, because the school's arithmetic is the one that counts.
    summary = parse_html(chapel_html)
    assert summary.allowed == 6
    assert summary.used == 3
    assert summary.remaining == 3


def test_html_reads_the_term(chapel_html: str) -> None:
    assert parse_html(chapel_html).term == "Fall 2026"


def test_html_with_no_table_raises_rather_than_reporting_zero() -> None:
    # Silently returning an empty summary would render as "0 skips used", which
    # is the app lying in the reassuring direction. Fail instead.
    with pytest.raises(ParseError):
        parse_html("<html><body><p>Nothing here.</p></body></html>")


def test_html_shifted_column_does_not_scramble_fields() -> None:
    """A column inserted upstream must not shift every field by one."""
    html = """
    <table>
      <thead><tr><th>#</th><th>Date</th><th>Status</th></tr></thead>
      <tbody><tr><td>1</td><td>09/11/2026</td><td>Absent</td></tr></tbody>
    </table>
    """
    record = parse_html(html).records[0]
    assert record.on == date(2026, 9, 11)
    assert record.status is AttendanceStatus.ABSENT


# ---------------------------------------------------------------------------
# JSON
# ---------------------------------------------------------------------------

def test_json_pascal_case(samples_dir: Path) -> None:
    data = json.loads((samples_dir / "chapel_json_pascal.json").read_text())
    summary = parse_json(data)

    assert len(summary.records) == 8
    assert summary.student_name == "Sample Student"
    assert summary.term == "Fall 2026"
    assert summary.allowed == 6
    assert summary.used == 3
    assert summary.records[0].on == date(2026, 8, 26)
    assert summary.records[3].status is AttendanceStatus.EXCUSED


def test_json_camel_case_under_an_envelope(samples_dir: Path) -> None:
    """The records list is three levels deep and there is no 'used' field."""
    data = json.loads((samples_dir / "chapel_json_camel.json").read_text())
    summary = parse_json(data)

    assert len(summary.records) == 8
    assert summary.allowed == 6
    # Reported as `skipsRemaining`, so `used` must be derived: 6 - 3.
    assert summary.used == 3
    assert summary.remaining == 3


def test_json_camel_reads_the_student_name_not_the_term(samples_dir: Path) -> None:
    """Regression: a substring match on "name" used to return "Fall 2026".

    `termName` contains "name". The exact-match pass has to win globally before
    any substring matching happens, or the summary header shows the term where
    the student's name belongs.
    """
    data = json.loads((samples_dir / "chapel_json_camel.json").read_text())
    summary = parse_json(data)
    assert summary.student_name == "Sample Student"
    assert summary.term == "Fall 2026"


def test_json_camel_does_not_mistake_allowed_for_used(samples_dir: Path) -> None:
    """Regression: "skips" used to substring-match `allowedSkips`, giving used=6."""
    data = json.loads((samples_dir / "chapel_json_camel.json").read_text())
    assert parse_json(data).used == 3


def test_json_bare_list_counts_skips_itself(samples_dir: Path) -> None:
    data = json.loads((samples_dir / "chapel_json_bare_list.json").read_text())
    summary = parse_json(data)

    assert len(summary.records) == 9
    assert summary.allowed is None          # the page never said
    assert summary.used == 3                # counted from the rows
    assert summary.remaining is None        # and so this is unknowable


def test_unknown_status_does_not_count_as_a_skip(samples_dir: Path) -> None:
    """Guessing high would make the app frighten you for no reason."""
    data = json.loads((samples_dir / "chapel_json_bare_list.json").read_text())
    unknown = parse_json(data).records[-1]

    assert unknown.status is AttendanceStatus.UNKNOWN
    assert unknown.counts_as_skip is False


# ---------------------------------------------------------------------------
# Dispatch
# ---------------------------------------------------------------------------

def test_parse_body_sniffs_html(chapel_html: str) -> None:
    assert len(parse_body(chapel_html).records) == 8


def test_parse_body_sniffs_json(samples_dir: Path) -> None:
    body = (samples_dir / "chapel_json_pascal.json").read_text()
    assert len(parse_body(body).records) == 8


def test_parse_body_trusts_the_body_over_the_content_type(chapel_html: str) -> None:
    """ASP.NET has been known to serve one thing and label it another."""
    assert len(parse_body(chapel_html, content_type="text/html; charset=utf-8").records) == 8


def test_parse_body_rejects_an_empty_response() -> None:
    with pytest.raises(ParseError):
        parse_body("")


# ---------------------------------------------------------------------------
# Dates
# ---------------------------------------------------------------------------

@pytest.mark.parametrize(
    "raw,expected",
    [
        ("2026-09-16", date(2026, 9, 16)),
        ("2026-09-16T00:00:00", date(2026, 9, 16)),
        ("2026-09-16T04:00:00Z", date(2026, 9, 16)),
        ("9/16/2026", date(2026, 9, 16)),
        ("09/16/2026", date(2026, 9, 16)),
        ("Sep 16, 2026", date(2026, 9, 16)),
        ("September 16, 2026", date(2026, 9, 16)),
        ("Tue 9/16/2026", date(2026, 9, 16)),
    ],
)
def test_parse_date_handles_the_formats_aspnet_emits(raw: str, expected: date) -> None:
    assert parse_date(raw) == expected


def test_parse_date_handles_the_legacy_microsoft_json_epoch() -> None:
    """``/Date(ms)/`` is milliseconds since the epoch, interpreted locally."""
    from datetime import datetime

    expected = datetime.fromtimestamp(1_788_400_000).date()
    assert parse_date("/Date(1788400000000)/") == expected


@pytest.mark.parametrize("raw", ["", None, "n/a", "TBD", "—"])
def test_parse_date_degrades_instead_of_raising(raw) -> None:
    # One unreadable cell must not cost us the whole page.
    assert parse_date(raw) is None


def test_unparseable_date_keeps_the_original_text() -> None:
    html = """
    <table>
      <thead><tr><th>Date</th><th>Status</th></tr></thead>
      <tbody><tr><td>sometime last week</td><td>Absent</td></tr></tbody>
    </table>
    """
    record = parse_html(html).records[0]
    assert record.on is None
    assert record.raw_date == "sometime last week"
    assert record.status is AttendanceStatus.ABSENT


# ---------------------------------------------------------------------------
# The provider, end to end over FixtureTransport
# ---------------------------------------------------------------------------

def test_provider_fetches_and_parses(fixtures_dir: Path) -> None:
    provider = ChapelProvider(FixtureTransport(fixtures_dir))
    summary = provider.fetch()

    assert isinstance(summary, ChapelSummary)
    assert len(summary.records) == 8


def test_provider_requests_the_canonical_path() -> None:
    # /CedarInfo/ChapelAttendance 301s here; requesting the destination saves a
    # round trip and is what the SAML RelayState will carry.
    assert ChapelProvider.path == CHAPEL_PATH == "/cedarinfo/chapelskip"


def test_a_login_page_raises_session_expired_instead_of_parse_error(login_html: str) -> None:
    """The single most important behaviour in the app.

    A sign-in page must never reach a parser. If it did, the user would see
    "the page format changed" when the truth is "log in again".
    """

    class ExpiredTransport:
        def get(self, path: str) -> Response:
            return Response(
                status=200,
                url="https://login.microsoftonline.com/81c32413-.../saml2?SAMLRequest=x",
                body=login_html,
            )

    with pytest.raises(SessionExpired):
        ChapelProvider(ExpiredTransport()).fetch()


def test_summary_derived_counts_only_unexcused() -> None:
    records = [
        ChapelRecord(on=date(2026, 9, 1), status=AttendanceStatus.ABSENT),
        ChapelRecord(on=date(2026, 9, 2), status=AttendanceStatus.EXCUSED),
        ChapelRecord(on=date(2026, 9, 3), status=AttendanceStatus.EXEMPT),
        ChapelRecord(on=date(2026, 9, 4), status=AttendanceStatus.PRESENT),
        ChapelRecord(on=date(2026, 9, 5), status=AttendanceStatus.UNKNOWN),
    ]
    summary = ChapelSummary.derived(records, allowed=6)

    assert summary.used == 1
    assert summary.remaining == 5
