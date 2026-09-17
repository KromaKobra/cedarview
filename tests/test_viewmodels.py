"""Viewmodel behaviour, headless.

These touch Qt (``QT_QPA_PLATFORM=offscreen`` is set in ``conftest.py``) but
never start an event loop or a thread: the completion handlers are invoked
directly, which is what makes the tests deterministic. The threading itself is
one line in ``mycu/ui/tasks.py`` and is exercised by running the app.
"""

from __future__ import annotations

from datetime import date
from pathlib import Path

import pytest

pytest.importorskip("PySide6.QtCore", reason="PySide6 not available")

from mycu.core.errors import ParseError, SessionExpired, TransportError  # noqa: E402
from mycu.core.models import AttendanceStatus, ChapelRecord, ChapelSummary  # noqa: E402
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
    assert names == {"dateText", "status", "title", "note", "countsAsSkip"}


def test_rows_expose_their_fields() -> None:
    model = ChapelListModel()
    model.replace([
        ChapelRecord(
            on=date(2026, 9, 11),
            status=AttendanceStatus.ABSENT,
            title="Faculty Chapel",
            note="",
        )
    ])

    index = model.index(0, 0)
    assert model.rowCount() == 1
    assert model.data(index, ChapelListModel.StatusRole) == "absent"
    assert model.data(index, ChapelListModel.TitleRole) == "Faculty Chapel"
    assert model.data(index, ChapelListModel.SkipRole) is True
    assert "Sep" in model.data(index, ChapelListModel.DateRole)


def test_an_unreadable_date_falls_back_to_its_original_text() -> None:
    model = ChapelListModel()
    model.replace([
        ChapelRecord(on=None, status=AttendanceStatus.UNKNOWN, raw_date="sometime last week")
    ])
    assert model.data(model.index(0, 0), ChapelListModel.DateRole) == "sometime last week"


def test_out_of_range_access_returns_none() -> None:
    model = ChapelListModel()
    assert model.data(model.index(5, 0), ChapelListModel.DateRole) is None


# ---------------------------------------------------------------------------
# Viewmodel
# ---------------------------------------------------------------------------

def test_starts_empty_and_not_loaded(vm: ChapelViewModel) -> None:
    assert vm.records.rowCount() == 0
    assert vm.loaded is False
    assert vm.busy is False
    assert vm.error == ""


def test_a_successful_load_populates_everything(vm: ChapelViewModel) -> None:
    vm._on_loaded(ChapelSummary.derived(
        [ChapelRecord(on=date(2026, 9, 11), status=AttendanceStatus.ABSENT)],
        allowed=6,
        term="Fall 2026",
    ))

    assert vm.loaded is True
    assert vm.busy is False
    assert vm.term == "Fall 2026"
    assert vm.used == 1
    assert vm.allowed == 6
    assert vm.remaining == 5
    assert vm.records.rowCount() == 1


def test_an_unknown_allowance_is_reported_as_minus_one(vm: ChapelViewModel) -> None:
    """QML has no null int, and 0 would render as "0 allowed" — a lie.

    ``ChapelView.qml`` checks for the sentinel and drops the denominator.
    """
    vm._on_loaded(ChapelSummary.derived([], allowed=None))
    assert vm.allowed == -1
    assert vm.remaining == -1


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
    vm._on_loaded(ChapelSummary.derived([]))
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
    first._on_loaded(ChapelSummary.derived([], term="Fall 2026"))

    second = ChapelViewModel(FixtureTransport(fixtures_dir), SessionStore(tmp_path))
    assert second.term == "Fall 2026"
