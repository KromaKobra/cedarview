"""Chapel skips, against REAL captured responses.

Fixtures are verbatim captures from a signed-in session on 2026-09-17, with the
student ID scrubbed to 1234567 and the name to "Sample Student". Structure,
counts and vocabulary are untouched — those are the thing being tested.

The previous version of this file tested a *guess*: a hand-written HTML table
with a status column. The real page turned out to be a Vue app calling three
JSON endpoints, and there is no per-session status anywhere — it is a ledger of
balance movements. That whole model is gone.
"""

from __future__ import annotations

import json
from datetime import datetime
from pathlib import Path

import pytest

from mycu.core.errors import ParseError, SessionExpired
from mycu.core.models import ChapelLedgerEntry, ChapelSummary
from mycu.core.providers.chapel import (
    CHAPEL_PATH,
    LEDGER_PATH,
    SUMMARY_PATH,
    ChapelProvider,
    build_summary,
    extract_student_id,
)
from mycu.core.transport import BASE_URL, FixtureTransport, Response


@pytest.fixture
def summary_json(fixtures_dir: Path):
    return json.loads((fixtures_dir / "cedarinfo_chapelskip_getstudentsummaryjson.json").read_text())


@pytest.fixture
def ledger_json(fixtures_dir: Path):
    return json.loads((fixtures_dir / "cedarinfo_chapelskip_getstudentledgerjson.json").read_text())


@pytest.fixture
def summary(summary_json, ledger_json) -> ChapelSummary:
    return build_summary(summary_json, ledger_json, [])


# ---------------------------------------------------------------------------
# The numbers
# ---------------------------------------------------------------------------

def test_reported_figures_are_used_verbatim(summary) -> None:
    assert summary.used == 2
    assert summary.total == 18
    assert summary.remaining == 16


def test_the_ledger_must_not_be_used_to_compute_skips_used(summary, ledger_json) -> None:
    """The single most important behaviour in this provider.

    The ledger's counts sum to 1 (+1, -1, +1). The server reports SkipsUsed: 2.
    Both are right on the server's terms — the -1 manual adjustment is *also*
    expressed as the +1 "Manual Arrangement" allowance line. Recomputing from
    the ledger shows a different, wrong number.
    """
    assert sum(e["Count"] for e in ledger_json) == 1
    assert summary.used == 2, "used must come from SkipsUsed, not from the ledger"


def test_the_allowance_breakdown_sums_to_the_total(summary) -> None:
    assert [line.reason for line in summary.allowance] == ["Skips Allowed", "Manual Arrangement"]
    assert [line.count for line in summary.allowance] == [17, 1]
    assert sum(line.count for line in summary.allowance) == summary.total


def test_allowance_descriptions_survive(summary) -> None:
    assert summary.allowance[0].description == "Base semester allowance"


# ---------------------------------------------------------------------------
# Identity and term
# ---------------------------------------------------------------------------

def test_term_and_name(summary) -> None:
    assert summary.term == "2026FA"
    assert summary.term_name == "Fall Semester 2026"
    assert summary.student_name == "Sample Student"


def test_label_prefers_the_readable_term(summary) -> None:
    assert summary.label == "Fall Semester 2026"
    assert ChapelSummary(term="2026FA").label == "2026FA"


def test_requirement_flags(summary) -> None:
    assert summary.is_required_to_attend is True
    assert summary.is_in_good_standing is True
    assert summary.status == "good"
    assert "Undergraduate Student" in summary.requirement_reasons
    assert len(summary.requirement_reasons) == 3


# ---------------------------------------------------------------------------
# The ledger
# ---------------------------------------------------------------------------

def test_every_ledger_entry_is_parsed(summary) -> None:
    assert len(summary.entries) == 3


def test_entries_are_newest_first(summary) -> None:
    created = [e.created_at for e in summary.entries]
    assert created == sorted(created, reverse=True)


def test_both_entry_types_appear(summary) -> None:
    assert {e.entry_type for e in summary.entries} == {"Chapel Skip", "Manual Adjustment"}


def test_a_manual_adjustment_can_be_negative(summary) -> None:
    adjustment = next(e for e in summary.entries if e.entry_type == "Manual Adjustment")
    assert adjustment.count == -1
    assert adjustment.is_skip is False
    assert adjustment.can_remove is True


def test_an_adjustment_has_no_chapel_date(summary) -> None:
    """ChapelDate is genuinely null for adjustments — they aren't tied to a chapel."""
    adjustment = next(e for e in summary.entries if e.entry_type == "Manual Adjustment")
    assert adjustment.on is None
    assert adjustment.reason == "Had ID replaced"


def test_an_undated_entry_still_has_something_true_to_show(summary) -> None:
    adjustment = next(e for e in summary.entries if e.on is None)
    assert adjustment.when == "Had ID replaced"


def test_a_skip_shows_its_date(summary) -> None:
    skip = next(e for e in summary.entries if e.entry_type == "Chapel Skip")
    assert skip.count == 1
    assert skip.is_skip is True
    assert skip.on is not None
    assert str(skip.on.year) in skip.when


def test_chapel_dates_parse(summary) -> None:
    dates = sorted(e.on for e in summary.entries if e.on)
    assert dates[0] == datetime(2026, 8, 17, 10, 0)
    assert dates[-1] == datetime(2026, 8, 20, 10, 0)


def test_no_fines_is_an_empty_tuple_not_an_error(summary) -> None:
    assert summary.fines == ()


# ---------------------------------------------------------------------------
# The student-ID bootstrap
# ---------------------------------------------------------------------------

def test_student_id_is_read_from_the_dashboard(fixtures_dir: Path) -> None:
    html = (fixtures_dir / "cedarinfo_chapelskip.html").read_text()
    assert extract_student_id(html) == "1234567"


@pytest.mark.parametrize(
    "snippet",
    [
        "const studentId = '1234567'",
        'const studentId="1234567"',
        "let studentId   =    '1234567' ;",
        "var studentId='1234567'",
    ],
)
def test_student_id_survives_reasonable_reformatting(snippet: str) -> None:
    assert extract_student_id(f"<script>{snippet}</script>") == "1234567"


def test_a_missing_bootstrap_says_what_to_do() -> None:
    with pytest.raises(ParseError, match="discover chapel"):
        extract_student_id("<html><body>nothing here</body></html>")


# ---------------------------------------------------------------------------
# Robustness
# ---------------------------------------------------------------------------

def test_a_non_object_summary_raises() -> None:
    with pytest.raises(ParseError):
        build_summary([1, 2, 3], [])


def test_a_non_list_ledger_raises() -> None:
    with pytest.raises(ParseError, match="list"):
        build_summary({"SkipsUsed": 0}, {"not": "a list"})


def test_an_empty_ledger_is_fine() -> None:
    result = build_summary({"SkipsUsed": 0, "SkipsTotal": 18, "SkipsRemaining": 18}, [])
    assert result.entries == ()
    assert result.remaining == 18


def test_a_malformed_ledger_entry_is_skipped() -> None:
    result = build_summary({}, ["nonsense", {"Count": 1, "EntryType": "Chapel Skip"}])
    assert len(result.entries) == 1


def test_an_unparseable_timestamp_costs_only_that_field() -> None:
    result = build_summary({}, [{"Count": 1, "ChapelDate": "whenever", "CreatedReason": "x"}])
    assert result.entries[0].on is None
    assert result.entries[0].reason == "x"


def test_missing_figures_stay_none_rather_than_becoming_zero() -> None:
    """Zero would render as "0 of 0 skips" — a confident lie."""
    result = build_summary({}, [])
    assert result.used is None
    assert result.total is None
    assert result.remaining is None


# ---------------------------------------------------------------------------
# The provider, end to end
# ---------------------------------------------------------------------------

def test_provider_fetches_all_three_endpoints(fixtures_dir: Path) -> None:
    result = ChapelProvider(FixtureTransport(fixtures_dir)).fetch()
    assert result.used == 2
    assert result.total == 18
    assert len(result.entries) == 3


def test_provider_requests_the_dashboard_then_the_json(fixtures_dir: Path) -> None:
    """Four requests: the page for the ID, then summary, ledger and fines."""
    inner = FixtureTransport(fixtures_dir)
    seen: list[str] = []

    class Recording:
        def get(self, path: str) -> Response:
            seen.append(path)
            return inner.get(path)

    ChapelProvider(Recording()).fetch()

    assert seen[0] == CHAPEL_PATH
    assert SUMMARY_PATH in seen[1] and "studentId=1234567" in seen[1]
    assert LEDGER_PATH in seen[2]
    assert len(seen) == 4


def test_the_student_id_is_fetched_once_and_reused(fixtures_dir: Path) -> None:
    inner = FixtureTransport(fixtures_dir)
    calls: list[str] = []

    class Recording:
        def get(self, path: str) -> Response:
            calls.append(path)
            return inner.get(path)

    provider = ChapelProvider(Recording())
    provider.fetch()
    provider.fetch()

    assert calls.count(CHAPEL_PATH) == 1, "the dashboard should not be re-fetched"


def test_fines_failing_does_not_cost_you_the_skip_count(fixtures_dir: Path) -> None:
    """The count is the point of the screen; fines are a nice-to-have."""
    inner = FixtureTransport(fixtures_dir)

    class FinesBroken:
        def get(self, path: str) -> Response:
            if "Fines" in path:
                raise RuntimeError("fines endpoint is having a day")
            return inner.get(path)

    result = ChapelProvider(FinesBroken()).fetch()
    assert result.used == 2
    assert result.fines == ()


def test_a_login_page_raises_session_expired_instead_of_parse_error(login_html: str) -> None:
    """A sign-in page must never reach a parser."""

    class Expired:
        def get(self, path: str) -> Response:
            return Response(
                status=200,
                url="https://login.microsoftonline.com/81c32413-.../saml2?SAMLRequest=x",
                body=login_html,
            )

    with pytest.raises(SessionExpired):
        ChapelProvider(Expired()).fetch()


def test_the_canonical_path_is_unchanged() -> None:
    assert ChapelProvider.path == CHAPEL_PATH == "/cedarinfo/chapelskip"
    assert BASE_URL == "https://selfservice.cedarville.edu"
