"""Viewmodel behaviour, headless.

These touch Qt (``QT_QPA_PLATFORM=offscreen`` is set in ``conftest.py``) but
never start an event loop or a thread: the completion handlers are invoked
directly, which is what makes the tests deterministic. The threading itself is
one line in ``mycu/ui/tasks.py`` and is exercised by running the app.
"""

from __future__ import annotations

from datetime import datetime
from pathlib import Path

import pytest

pytest.importorskip("PySide6.QtCore", reason="PySide6 not available")

from mycu.core.errors import ParseError, SessionExpired, TransportError  # noqa: E402
from mycu.core.models import AllowanceLine, ChapelLedgerEntry, ChapelSummary  # noqa: E402
from mycu.core.session import SessionStore  # noqa: E402
from mycu.core.transport import FixtureTransport  # noqa: E402
from mycu.ui.viewmodels.chapel import ChapelListModel, ChapelViewModel  # noqa: E402


@pytest.fixture
def vm(tmp_path: Path, fixtures_dir: Path) -> ChapelViewModel:
    return ChapelViewModel(FixtureTransport(fixtures_dir), SessionStore(tmp_path))


# ---------------------------------------------------------------------------
# List model
# ---------------------------------------------------------------------------

def test_roles_are_named_for_qml() -> None:
    names = {bytes(v).decode() for v in ChapelListModel().roleNames().values()}
    assert names == {"whenText", "reason", "entryType", "count", "isSkip"}


def test_a_skip_row_exposes_its_fields() -> None:
    model = ChapelListModel()
    model.replace([
        ChapelLedgerEntry(
            on=datetime(2026, 8, 20, 10, 0),
            count=1,
            entry_type="Chapel Skip",
            reason="Absent from Chapel 8/20/2026",
        )
    ])

    index = model.index(0, 0)
    assert model.rowCount() == 1
    assert model.data(index, ChapelListModel.CountRole) == 1
    assert model.data(index, ChapelListModel.SkipRole) is True
    assert model.data(index, ChapelListModel.TypeRole) == "Chapel Skip"
    assert "Aug" in model.data(index, ChapelListModel.WhenRole)


def test_a_manual_adjustment_keeps_its_negative_count() -> None:
    """It gave a skip back; it must not render like another absence."""
    model = ChapelListModel()
    model.replace([
        ChapelLedgerEntry(on=None, count=-1, entry_type="Manual Adjustment",
                          reason="Had ID replaced")
    ])
    index = model.index(0, 0)
    assert model.data(index, ChapelListModel.CountRole) == -1
    assert model.data(index, ChapelListModel.SkipRole) is False


def test_an_undated_entry_shows_its_reason_instead_of_a_date() -> None:
    model = ChapelListModel()
    model.replace([
        ChapelLedgerEntry(on=None, count=-1, entry_type="Manual Adjustment",
                          reason="Had ID replaced")
    ])
    index = model.index(0, 0)
    assert model.data(index, ChapelListModel.WhenRole) == "Had ID replaced"
    # …and the reason is not then repeated on the second line.
    assert model.data(index, ChapelListModel.ReasonRole) == ""


def test_out_of_range_access_returns_none() -> None:
    model = ChapelListModel()
    assert model.data(model.index(5, 0), ChapelListModel.WhenRole) is None


# ---------------------------------------------------------------------------
# Viewmodel
# ---------------------------------------------------------------------------

def test_starts_empty_and_not_loaded(vm: ChapelViewModel) -> None:
    assert vm.records.rowCount() == 0
    assert vm.loaded is False
    assert vm.busy is False
    assert vm.error == ""


def test_a_successful_load_populates_everything(vm: ChapelViewModel) -> None:
    vm._on_loaded(ChapelSummary(
        used=2, total=18, remaining=16,
        term="2026FA", term_name="Fall Semester 2026",
        entries=(ChapelLedgerEntry(on=datetime(2026, 8, 20, 10, 0), count=1),),
    ))

    assert vm.loaded is True
    assert vm.busy is False
    assert vm.term == "Fall Semester 2026"
    assert vm.used == 2
    assert vm.allowed == 18
    assert vm.remaining == 16
    assert vm.records.rowCount() == 1


def test_figures_are_passed_through_not_recomputed(vm: ChapelViewModel) -> None:
    """The ledger sums to 1 here; the server says 2. The server wins."""
    vm._on_loaded(ChapelSummary(
        used=2, total=18, remaining=16,
        entries=(
            ChapelLedgerEntry(on=None, count=1),
            ChapelLedgerEntry(on=None, count=-1),
            ChapelLedgerEntry(on=None, count=1),
        ),
    ))
    assert vm.used == 2


def test_unknown_figures_are_reported_as_minus_one(vm: ChapelViewModel) -> None:
    """QML has no null int, and 0 would render as "0 of 0 skips" — a lie."""
    vm._on_loaded(ChapelSummary())
    assert vm.used == -1
    assert vm.allowed == -1
    assert vm.remaining == -1


def test_the_allowance_breakdown_explains_an_unexpected_total(vm: ChapelViewModel) -> None:
    vm._on_loaded(ChapelSummary(
        used=2, total=18, remaining=16,
        allowance=(
            AllowanceLine(reason="Skips Allowed", count=17),
            AllowanceLine(reason="Manual Arrangement", count=1),
        ),
    ))
    assert vm.allowanceText == "17 skips allowed + 1 manual arrangement"


def test_a_single_allowance_line_needs_no_explanation(vm: ChapelViewModel) -> None:
    vm._on_loaded(ChapelSummary(
        used=0, total=17,
        allowance=(AllowanceLine(reason="Skips Allowed", count=17),),
    ))
    assert vm.allowanceText == ""


def test_expiry_is_signalled_and_never_shown_as_an_error(vm: ChapelViewModel) -> None:
    """Re-authenticating is part of the lifecycle, not a failure to report."""
    seen = []
    vm.sessionExpired.connect(lambda: seen.append(True))

    vm._on_failed(SessionExpired("gone"))

    assert seen == [True]
    assert vm.error == ""
    assert vm.busy is False


def test_a_parse_error_tells_you_where_to_look(vm: ChapelViewModel) -> None:
    vm._on_failed(ParseError("no table"))
    assert "check-live" in vm.error


def test_a_transport_error_is_reported_as_reachability(vm: ChapelViewModel) -> None:
    vm._on_failed(TransportError("timed out"))
    assert "Couldn't reach" in vm.error


def test_an_unexpected_exception_still_reaches_the_user(vm: ChapelViewModel) -> None:
    vm._on_failed(ZeroDivisionError("boom"))
    assert "Unexpected error" in vm.error


def test_a_successful_load_clears_a_previous_error(vm: ChapelViewModel) -> None:
    vm._on_failed(TransportError("timed out"))
    assert vm.error
    vm._on_loaded(ChapelSummary(used=0, total=18, remaining=18))
    assert vm.error == ""


def test_refresh_is_not_re_entrant(vm: ChapelViewModel) -> None:
    """A second tap on ↻ while busy must not start a second fetch."""
    started = []
    vm._busy = True
    vm.changed.connect(lambda: started.append(True))

    vm.refresh()

    assert started == []   # no state change emitted, so nothing was kicked off


def test_the_term_is_remembered_across_launches(tmp_path: Path, fixtures_dir: Path) -> None:
    store = SessionStore(tmp_path)
    first = ChapelViewModel(FixtureTransport(fixtures_dir), store)
    first._on_loaded(ChapelSummary(term_name="Fall Semester 2026"))

    second = ChapelViewModel(FixtureTransport(fixtures_dir), SessionStore(tmp_path))
    assert second.term == "Fall Semester 2026"
