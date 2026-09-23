"""Summary screen: how much of the semester is left.

The odd one out among the viewmodels. Every other one wraps a provider, a
worker thread and an error path; this one wraps :mod:`mycu.core.calendar` and a
clock. There is no ``refresh()`` and no ``busy`` — there is nothing to fetch,
because Cedarville does not publish term boundaries anywhere the app can read
(see the module docstring over in ``core/calendar.py``).

It is still a viewmodel rather than a few expressions in QML, for two reasons:
the date formatting has to dodge the same glibc-only ``strftime`` codes the
chapel viewmodel dodges (``%-d`` does not exist on Android's bionic), and
``tests/test_qml_contract.py`` only guards bindings whose backing object is a
registered viewmodel. Free-floating JS in the QML would be checked by nothing.

Exposed to QML as the context property ``semester``.
"""

from __future__ import annotations

import logging
from datetime import date

from PySide6.QtCore import Property, QObject, Signal, Slot

from ...core.calendar import current_term, next_term_start

log = logging.getLogger(__name__)


def _short_date(day: date) -> str:
    """"Fri, Dec 11".

    Built by hand rather than with ``%-d`` because that is a glibc extension and
    Android's bionic does not have it — the same trap
    :mod:`mycu.ui.viewmodels.chapel` documents. ``%d`` would give "Dec 05".
    """
    return f"{day.strftime('%a')}, {day.strftime('%b')} {day.day}"


class SemesterViewModel(QObject):
    """Where we are in the term, for the countdown card."""

    changed = Signal()

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._today = date.today()

    # ------------------------------------------------------------------
    # Properties
    # ------------------------------------------------------------------

    @Property(bool, notify=changed)
    def inTerm(self) -> bool:
        """False over the summer and the Christmas break.

        The QML reads this to swap the whole card between a countdown and a
        "Spring starts …" line, rather than rendering a confident zero.
        """
        return current_term(self._today) is not None

    @Property(str, notify=changed)
    def termName(self) -> str:
        """"Fall" / "Spring", or "" between terms."""
        term = current_term(self._today)
        return term.name if term else ""

    @Property(int, notify=changed)
    def daysLeft(self) -> int:
        """Days to the last day of term, or ``-1`` when there is no term.

        ``-1`` rather than ``0`` for the same reason the chapel figures use it
        (see :class:`~mycu.ui.viewmodels.chapel.ChapelViewModel`): QML has no
        null int, and a zero here would read as "the semester ends today".
        """
        term = current_term(self._today)
        return term.days_left(self._today) if term else -1

    @Property(int, notify=changed)
    def totalDays(self) -> int:
        """The countdown's starting figure — the "/ 114" — or ``-1`` between terms."""
        term = current_term(self._today)
        return term.total_days(self._today.year) if term else -1

    @Property(float, notify=changed)
    def elapsedFraction(self) -> float:
        """0.0–1.0 of the term completed, for ``MeterBar``. Zero between terms,
        where the bar hides."""
        term = current_term(self._today)
        return term.elapsed_fraction(self._today) if term else 0.0

    @Property(str, notify=changed)
    def endDateText(self) -> str:
        """"ends Fri, Dec 11" — the date the countdown is counting to."""
        term = current_term(self._today)
        if term is None:
            return ""
        return "ends " + _short_date(term.end_in(self._today.year))

    @Property(str, notify=changed)
    def nextTermText(self) -> str:
        """"Spring starts Mon, Jan 5" — the between-terms line, else ""."""
        upcoming = next_term_start(self._today)
        if upcoming is None:
            return ""
        term, starts = upcoming
        return f"{term.name} starts {_short_date(starts)}"

    # ------------------------------------------------------------------
    # Actions
    # ------------------------------------------------------------------

    @Slot()
    def refreshAll(self) -> None:
        """Re-read the clock.

        Named to match the other viewmodels so the refresh gesture can call it
        without a special case. It exists because the app is left running
        overnight on a phone more often than it is restarted, and a countdown
        that is a day stale is a countdown that is wrong.
        """
        today = date.today()
        if today == self._today:
            return
        self._today = today
        log.info("semester: date rolled over to %s", today)
        self.changed.emit()
