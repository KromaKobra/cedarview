"""Dining screen: Home Cooking for every meal on a chosen day."""

from __future__ import annotations

import logging
from datetime import date, timedelta

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

from ...core.errors import ParseError, TransportError
from ...core.models import HOME_COOKING, DayMenu
from ...core.providers.dining import DEFAULT_DAYS, DiningProvider
from ..tasks import run_in_background

log = logging.getLogger(__name__)


class MenuListModel(QAbstractListModel):
    """A flat list of headers and dishes, for a QML ``ListView``.

    Flat rather than nested on purpose: QML list views want one model, and a
    tuple-of-tuples cannot be bound without a second model class per level. A
    row is either a meal header (``isHeader``) or a dish, and the delegate picks
    a look from that. It also means section headers scroll naturally with their
    items, which is what you want on a phone.
    """

    TextRole = Qt.ItemDataRole.UserRole + 1
    AllergenRole = Qt.ItemDataRole.UserRole + 2
    HeaderRole = Qt.ItemDataRole.UserRole + 3

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._rows: list[tuple[str, str, bool]] = []

    def roleNames(self) -> dict[int, QByteArray]:
        return {
            self.TextRole: QByteArray(b"text"),
            self.AllergenRole: QByteArray(b"allergens"),
            self.HeaderRole: QByteArray(b"isHeader"),
        }

    def rowCount(self, parent: QModelIndex = QModelIndex()) -> int:
        return 0 if parent.isValid() else len(self._rows)

    def data(self, index: QModelIndex, role: int = Qt.ItemDataRole.DisplayRole):
        if not index.isValid() or not 0 <= index.row() < len(self._rows):
            return None

        text, allergens, is_header = self._rows[index.row()]
        if role == self.TextRole:
            return text
        if role == self.AllergenRole:
            return allergens
        if role == self.HeaderRole:
            return is_header
        return None

    def replace_from_blocks(self, blocks) -> None:
        self.beginResetModel()
        self._rows = []
        for block in blocks:
            self._rows.append((block.heading, "", True))
            for item in block.items:
                self._rows.append((item.name, item.allergen_text, False))
        self.endResetModel()


class DiningViewModel(QObject):
    """State and actions for the dining screen.

    Fetches a week at a time and pages through it locally — the payload is
    small, the menu does not change minute to minute, and a request per day
    would be rude to a service that is reformatting someone else's data.
    """

    changed = Signal()

    def __init__(self, transport, days: int = DEFAULT_DAYS, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._transport = transport
        self._days = days
        self._model = MenuListModel(self)
        self._menus: tuple[DayMenu, ...] = ()
        self._offset = 0
        self._busy = False
        self._error = ""
        self._loaded = False

    # ------------------------------------------------------------------

    @Property(QObject, constant=True)
    def items(self) -> MenuListModel:
        return self._model

    @Property(bool, notify=changed)
    def busy(self) -> bool:
        return self._busy

    @Property(bool, notify=changed)
    def loaded(self) -> bool:
        return self._loaded

    @Property(str, notify=changed)
    def error(self) -> str:
        return self._error

    @Property(str, notify=changed)
    def venue(self) -> str:
        return HOME_COOKING

    @Property(str, notify=changed)
    def dateText(self) -> str:
        """"Today", "Tomorrow", or a spelled-out date.

        Built by hand rather than with ``%-d``: the no-pad strftime flag is a
        glibc extension that Android's bionic libc does not have.
        """
        day = self._selected_date()
        if self._offset == 0:
            return "Today"
        if self._offset == 1:
            return "Tomorrow"
        return f"{day.strftime('%a %b')} {day.day}"

    @Property(int, notify=changed)
    def dayOffset(self) -> int:
        return self._offset

    @Property(bool, notify=changed)
    def canGoBack(self) -> bool:
        return self._offset > 0

    @Property(bool, notify=changed)
    def canGoForward(self) -> bool:
        """Whether another day is actually in the payload.

        The API serves forward from today only, so paging past the end would
        show a blank screen and look broken.
        """
        return any(d.on > self._selected_date() for d in self._menus)

    # ------------------------------------------------------------------

    @Slot()
    def refresh(self) -> None:
        if self._busy:
            return

        self._busy = True
        self._error = ""
        self.changed.emit()

        provider = DiningProvider(self._transport, days=self._days)
        run_in_background(provider.fetch, self._on_loaded, self._on_failed)

    @Slot()
    def nextDay(self) -> None:
        if self.canGoForward:
            self._offset += 1
            self._rebuild()

    @Slot()
    def previousDay(self) -> None:
        if self.canGoBack:
            self._offset -= 1
            self._rebuild()

    # ------------------------------------------------------------------

    def _selected_date(self) -> date:
        return date.today() + timedelta(days=self._offset)

    def _rebuild(self) -> None:
        target = self._selected_date()
        blocks = ()
        for day in self._menus:
            if day.on == target:
                blocks = day.for_venue(HOME_COOKING)
                break
        self._model.replace_from_blocks(blocks)
        self.changed.emit()

    @Slot(object)
    def _on_loaded(self, menus: object) -> None:
        self._menus = tuple(menus)  # type: ignore[arg-type]
        self._busy = False
        self._loaded = True
        self._error = ""
        log.info("dining: %d days loaded", len(self._menus))
        self._rebuild()

    @Slot(object)
    def _on_failed(self, exc: object) -> None:
        self._busy = False
        if isinstance(exc, ParseError):
            self._error = "The dining menu feed changed shape."
        elif isinstance(exc, TransportError):
            self._error = f"Couldn't reach the dining menu service: {exc}"
        else:
            self._error = f"Unexpected error: {exc}"
        log.error("dining refresh failed: %r", exc)
        self.changed.emit()
