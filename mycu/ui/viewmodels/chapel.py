"""Chapel screen: list model + viewmodel."""

from __future__ import annotations

import logging
from datetime import date, timedelta
from typing import Any

from PySide6.QtCore import (
    QAbstractListModel,
    QByteArray,
    QModelIndex,
    QObject,
    Property,
    Qt,
    Signal,
    Slot,
)

from ...core.errors import ParseError, SessionExpired, TransportError
from ...core.models import ChapelLedgerEntry, ChapelSummary, UpcomingChapel
from ...core.providers.chapel import ChapelProvider
from ...core.providers.chapel_schedule import ChapelScheduleProvider, next_chapel
from ...core.session import SessionStore
from ..tasks import run_in_background

log = logging.getLogger(__name__)


class ChapelListModel(QAbstractListModel):
    """Exposes the chapel-skip ledger to a QML ``ListView``.

    Rows are *balance movements*, not attendance. ``whenText`` is pre-formatted
    here because the fallback — show the reason when there is no chapel date,
    which is the normal case for manual adjustments — is a data decision, not a
    presentation one.
    """

    WhenRole = Qt.ItemDataRole.UserRole + 1
    ReasonRole = Qt.ItemDataRole.UserRole + 2
    TypeRole = Qt.ItemDataRole.UserRole + 3
    CountRole = Qt.ItemDataRole.UserRole + 4
    SkipRole = Qt.ItemDataRole.UserRole + 5

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._entries: list[ChapelLedgerEntry] = []

    def roleNames(self) -> dict[int, QByteArray]:
        return {
            self.WhenRole: QByteArray(b"whenText"),
            self.ReasonRole: QByteArray(b"reason"),
            self.TypeRole: QByteArray(b"entryType"),
            self.CountRole: QByteArray(b"count"),
            self.SkipRole: QByteArray(b"isSkip"),
        }

    def rowCount(self, parent: QModelIndex = QModelIndex()) -> int:
        return 0 if parent.isValid() else len(self._entries)

    def data(self, index: QModelIndex, role: int = Qt.ItemDataRole.DisplayRole) -> Any:
        if not index.isValid() or not 0 <= index.row() < len(self._entries):
            return None

        entry = self._entries[index.row()]
        if role == self.WhenRole:
            return entry.when
        if role == self.ReasonRole:
            # Suppress the reason when `when` is already showing it, which
            # happens for every undated adjustment.
            return "" if entry.when == entry.reason else entry.reason
        if role == self.TypeRole:
            return entry.entry_type
        if role == self.CountRole:
            # Signed, and rendered as "+1"/"-1" by the delegate: a manual
            # adjustment giving a skip back should not look like another skip.
            return entry.count
        if role == self.SkipRole:
            return entry.is_skip
        return None

    def replace(self, entries: "list[ChapelLedgerEntry] | tuple[ChapelLedgerEntry, ...]") -> None:
        self.beginResetModel()
        self._entries = list(entries)
        self.endResetModel()


class ChapelViewModel(QObject):
    """State and actions for the chapel screen.

    Exposed to QML as the context property ``chapel``.
    """

    changed = Signal()

    #: The provider hit an expired session. ``app.py`` wires this to
    #: :meth:`mycu.ui.login.LoginController.onSessionExpired`, which reopens the
    #: sign-in surface and then re-triggers :meth:`refresh`.
    sessionExpired = Signal()

    def __init__(
        self,
        transport,
        store: SessionStore,
        parent: QObject | None = None,
    ) -> None:
        super().__init__(parent)
        self._transport = transport
        self._store = store
        self._state = store.load()
        self._model = ChapelListModel(self)
        self._summary = ChapelSummary()
        self._busy = False
        self._error = ""
        self._loaded = False

        # The upcoming-chapel feed is a *different*, unauthenticated service
        # (mediaserve.cedarville.edu). It is on this screen because that is
        # where it belongs to a reader, not because it shares a source — so it
        # loads independently and a failure in one never blanks the other.
        self._next: UpcomingChapel | None = None

    # ------------------------------------------------------------------
    # Properties
    # ------------------------------------------------------------------

    @Property(QObject, constant=True)
    def records(self) -> ChapelListModel:
        return self._model

    @Property(bool, notify=changed)
    def busy(self) -> bool:
        return self._busy

    @Property(bool, notify=changed)
    def loaded(self) -> bool:
        """True once a fetch has succeeded at least once this run."""
        return self._loaded

    @Property(str, notify=changed)
    def error(self) -> str:
        return self._error

    @Property(str, notify=changed)
    def term(self) -> str:
        return self._summary.label or self._state.last_term

    @Property(str, notify=changed)
    def studentName(self) -> str:
        return self._summary.student_name

    # `used`, `allowed` and `remaining` are Cedarville's own figures, passed
    # through untouched. -1 is the "not known" sentinel: QML has no null int,
    # and 0 would render as "0 of 0 skips" — a confident lie.

    @Property(int, notify=changed)
    def used(self) -> int:
        return self._summary.used if self._summary.used is not None else -1

    @Property(int, notify=changed)
    def allowed(self) -> int:
        return self._summary.total if self._summary.total is not None else -1

    @Property(int, notify=changed)
    def remaining(self) -> int:
        return self._summary.remaining if self._summary.remaining is not None else -1

    @Property(str, notify=changed)
    def allowanceText(self) -> str:
        """How the total is made up — "17 base + 1 manual arrangement".

        Worth surfacing: it is the only place the app can explain why the total
        is 18 rather than the 17 everyone expects.
        """
        if len(self._summary.allowance) < 2:
            return ""
        return " + ".join(
            f"{line.count} {line.reason.lower()}" for line in self._summary.allowance
        )

    @Property(bool, notify=changed)
    def inGoodStanding(self) -> bool:
        return self._summary.is_in_good_standing

    @Property(bool, notify=changed)
    def requiredToAttend(self) -> bool:
        return self._summary.is_required_to_attend

    @Property(float, notify=changed)
    def remainingFraction(self) -> float:
        """How much of the allowance is left, 0.0–1.0, for the progress bar.

        ``0.0`` when either figure is unknown, which the QML reads together
        with ``remaining >= 0`` to hide the bar rather than draw an empty one.
        Clamped because the server's ``used`` and ``total`` come from different
        halves of its own arithmetic (see :class:`ChapelSummary`) and are not
        guaranteed to agree — a bar overflowing its track would look broken
        where a full bar just looks full.
        """
        total, remaining = self._summary.total, self._summary.remaining
        if not total or total <= 0 or remaining is None:
            return 0.0
        return max(0.0, min(1.0, remaining / total))

    # ------------------------------------------------------------------
    # Actions
    # ------------------------------------------------------------------

    @Property(str, notify=changed)
    def nextSpeaker(self) -> str:
        """Who is speaking at the next chapel, or "" if unknown.

        Empty rather than a placeholder so the QML can hide the whole row: over
        the summer there genuinely is no next chapel, and "TBA" would be a
        claim we cannot support.
        """
        return self._next.who if self._next else ""

    @Property(str, notify=changed)
    def nextChapelWhen(self) -> str:
        """"Today 10:00 AM" / "Tomorrow 10:00 AM" / "Mon 10:00 AM"."""
        if self._next is None or self._next.starts_at is None:
            return ""

        when = self._next.starts_at
        today = date.today()
        if when.date() == today:
            day = "Today"
        elif when.date() == today + timedelta(days=1):
            day = "Tomorrow"
        else:
            day = when.strftime("%a")

        # %-I is a glibc extension; bionic (Android) does not have it.
        hour = when.hour % 12 or 12
        return f"{day} {hour}:{when.minute:02d} {when.strftime('%p')}"

    @Property(str, notify=changed)
    def nextChapelDay(self) -> str:
        """"Today" / "Tomorrow" / "Friday" — the summary screen's badge.

        Split out from :attr:`nextChapelWhen` rather than parsed back out of
        it: the badge and the date line are two separate pieces of text on the
        summary card, and slicing a formatted string to get one of them back is
        how a UI ends up displaying "Tomorrow 10:00" in a pill.
        """
        if self._next is None or self._next.starts_at is None:
            return ""

        when = self._next.starts_at.date()
        today = date.today()
        if when == today:
            return "Today"
        if when == today + timedelta(days=1):
            return "Tomorrow"
        return self._next.starts_at.strftime("%A")

    @Property(str, notify=changed)
    def nextChapelDateText(self) -> str:
        """"Fri, Sep 18 · 10:00 AM" — the exact when, under the headline.

        The badge says "Tomorrow"; this says which day that actually is, which
        is the thing you need when deciding whether to set an alarm.
        """
        if self._next is None or self._next.starts_at is None:
            return ""

        when = self._next.starts_at
        # %-d and %-I are glibc extensions; bionic (Android) has neither.
        hour = when.hour % 12 or 12
        return (
            f"{when.strftime('%a')}, {when.strftime('%b')} {when.day}"
            f" · {hour}:{when.minute:02d} {when.strftime('%p')}"
        )

    @Property(str, notify=changed)
    def nextChapelTitle(self) -> str:
        """The event name, but only when it adds something.

        The API sets Title to the speaker's name verbatim, so showing both
        gives "Garrett Kell — Garrett Kell".
        """
        if self._next is None or self._next.is_same_as_title:
            return ""
        return self._next.title

    @Slot()
    def refreshSchedule(self) -> None:
        """Load the upcoming-chapel feed. Needs no session."""
        provider = ChapelScheduleProvider(self._transport)

        def done(chapels: object) -> None:
            self._next = next_chapel(tuple(chapels))  # type: ignore[arg-type]
            log.info("chapel schedule: next is %s", self._next.who if self._next else "(none)")
            self.changed.emit()

        def failed(exc: object) -> None:
            # Deliberately silent: the attendance figures are the point of this
            # screen, and losing the speaker line is not worth an error banner.
            log.warning("chapel schedule unavailable: %r", exc)

        run_in_background(provider.fetch, done, failed)

    @Slot()
    def refreshAll(self) -> None:
        """Everything on this screen.

        What the Refresh button and pull-to-refresh call. The screen draws on
        two unrelated sources — the authenticated skip ledger and the public
        upcoming-chapel feed — and a reader pulling down means "update what I
        am looking at", not "update the half of it that needs a session".
        Keeping the composition here means a third source is wired in one
        place rather than in every gesture handler.
        """
        self.refresh()
        self.refreshSchedule()

    @Slot()
    def refresh(self) -> None:
        """Fetch and parse chapel attendance on a worker thread.

        Cache-and-refresh-on-demand, never polling. This is one student reading
        their own record; it should behave like it.
        """
        if self._busy:
            return

        self._busy = True
        self._error = ""
        self.changed.emit()

        provider = ChapelProvider(self._transport)
        run_in_background(provider.fetch, self._on_loaded, self._on_failed)

    @Slot(object)
    def _on_loaded(self, summary: object) -> None:
        assert isinstance(summary, ChapelSummary)
        self._summary = summary
        self._model.replace(summary.entries)
        self._busy = False
        self._loaded = True
        self._error = ""

        self._state.last_term = summary.label or self._state.last_term
        self._store.mark_success(self._state)

        log.info(
            "chapel: %d ledger entries, used=%s of %s, remaining=%s",
            len(summary.entries), summary.used, summary.total, summary.remaining,
        )
        self.changed.emit()

    @Slot(object)
    def _on_failed(self, exc: object) -> None:
        self._busy = False

        if isinstance(exc, SessionExpired):
            # Not an error the user should read — it is a normal part of the
            # lifecycle. Hand it to the login controller and stay quiet.
            self._error = ""
            self.changed.emit()
            self.sessionExpired.emit()
            return

        if isinstance(exc, ParseError):
            self._error = (
                "Cedarville's chapel page didn't look the way this app expects. "
                "It has probably changed — run scripts/check-live to confirm."
            )
        elif isinstance(exc, TransportError):
            self._error = f"Couldn't reach Self-Service: {exc}"
        else:
            self._error = f"Unexpected error: {exc}"

        log.error("chapel refresh failed: %r", exc)
        self.changed.emit()
