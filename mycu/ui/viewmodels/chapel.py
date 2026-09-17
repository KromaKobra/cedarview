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
from ...core.models import ChapelRecord, ChapelSummary, UpcomingChapel
from ...core.providers.chapel import ChapelProvider
from ...core.providers.chapel_schedule import ChapelScheduleProvider, next_chapel
from ...core.session import SessionStore
from ..tasks import run_in_background

log = logging.getLogger(__name__)


class ChapelListModel(QAbstractListModel):
    """Exposes :class:`ChapelRecord` rows to a QML ``ListView``.

    Roles are named so QML reads ``model.dateText``, ``model.status`` and so on.
    ``dateText`` is pre-formatted here rather than in QML because the fallback
    (show the original string when the date would not parse) is a data decision,
    not a presentation one.
    """

    DateRole = Qt.ItemDataRole.UserRole + 1
    StatusRole = Qt.ItemDataRole.UserRole + 2
    TitleRole = Qt.ItemDataRole.UserRole + 3
    NoteRole = Qt.ItemDataRole.UserRole + 4
    SkipRole = Qt.ItemDataRole.UserRole + 5

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._records: list[ChapelRecord] = []

    def roleNames(self) -> dict[int, QByteArray]:
        return {
            self.DateRole: QByteArray(b"dateText"),
            self.StatusRole: QByteArray(b"status"),
            self.TitleRole: QByteArray(b"title"),
            self.NoteRole: QByteArray(b"note"),
            self.SkipRole: QByteArray(b"countsAsSkip"),
        }

    def rowCount(self, parent: QModelIndex = QModelIndex()) -> int:
        return 0 if parent.isValid() else len(self._records)

    def data(self, index: QModelIndex, role: int = Qt.ItemDataRole.DisplayRole) -> Any:
        if not index.isValid() or not 0 <= index.row() < len(self._records):
            return None

        record = self._records[index.row()]
        if role == self.DateRole:
            if record.on is not None:
                # Built by hand rather than with "%-d": the no-pad flag is a
                # glibc extension and Android's bionic libc does not have it.
                return (
                    f"{record.on.strftime('%a %b')} "
                    f"{record.on.day}, {record.on.year}"
                )
            return record.raw_date or "—"
        if role == self.StatusRole:
            return record.status.value
        if role == self.TitleRole:
            return record.title
        if role == self.NoteRole:
            return record.note
        if role == self.SkipRole:
            return record.counts_as_skip
        return None

    def replace(self, records: "list[ChapelRecord] | tuple[ChapelRecord, ...]") -> None:
        self.beginResetModel()
        self._records = list(records)
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
        return self._summary.term or self._state.last_term

    @Property(str, notify=changed)
    def studentName(self) -> str:
        return self._summary.student_name

    @Property(int, notify=changed)
    def used(self) -> int:
        return self._summary.used if self._summary.used is not None else 0

    @Property(int, notify=changed)
    def allowed(self) -> int:
        """Allowed skips, or ``-1`` when the page did not tell us.

        ``-1`` rather than ``0`` because QML has no null int, and zero would
        render as "0 skips allowed" — a confident lie. The QML checks for the
        sentinel and shows the count without a denominator instead.
        """
        return self._summary.allowed if self._summary.allowed is not None else -1

    @Property(int, notify=changed)
    def remaining(self) -> int:
        remaining = self._summary.remaining
        return remaining if remaining is not None else -1

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
        self._model.replace(summary.records)
        self._busy = False
        self._loaded = True
        self._error = ""

        self._state.last_term = summary.term or self._state.last_term
        self._store.mark_success(self._state)

        log.info(
            "chapel: %d records, used=%s allowed=%s",
            len(summary.records), summary.used, summary.allowed,
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
