"""Dining: the Home Cooking menu (Summary card) and meal-plan balances and activity (Summary and Dining tabs)."""

from __future__ import annotations

import logging
from datetime import date, datetime, timedelta

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
from ...core.models import HOME_COOKING, DayMenu, MealPlan, MealTransaction
from ...core.providers.dining import DEFAULT_DAYS, DiningProvider, next_meal_block
from ...core.providers.meals import MealsProvider
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

    def replace_items(self, items) -> None:
        """Dishes only, no heading row.

        The summary screen's card already carries the meal name in its own
        header, so a "BREAKFAST" row inside the list would say it twice.
        """
        self.beginResetModel()
        self._rows = [(item.name, item.allergen_text, False) for item in items]
        self.endResetModel()


class ActivityListModel(QAbstractListModel):
    """Recent meal-plan activity, flat, with a header row per day.

    Flat for the same reason as :class:`MenuListModel`: one model, one
    ``Repeater``, and the delegate picks a look from ``isHeader``.
    """

    HeaderRole = Qt.ItemDataRole.UserRole + 1
    TitleRole = Qt.ItemDataRole.UserRole + 2
    DetailRole = Qt.ItemDataRole.UserRole + 3
    AmountRole = Qt.ItemDataRole.UserRole + 4
    FlexRole = Qt.ItemDataRole.UserRole + 5
    DepositRole = Qt.ItemDataRole.UserRole + 6

    _ROLES = {
        HeaderRole: b"isHeader",
        TitleRole: b"title",
        DetailRole: b"detail",
        AmountRole: b"amount",
        FlexRole: b"isFlex",
        DepositRole: b"isDeposit",
    }

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        # (isHeader, title, detail, amount, isFlex, isDeposit), in role order.
        self._rows: list[tuple[bool, str, str, str, bool, bool]] = []

    def roleNames(self) -> dict[int, QByteArray]:
        return {role: QByteArray(name) for role, name in self._ROLES.items()}

    def rowCount(self, parent: QModelIndex = QModelIndex()) -> int:
        return 0 if parent.isValid() else len(self._rows)

    def data(self, index: QModelIndex, role: int = Qt.ItemDataRole.DisplayRole):
        if not index.isValid() or not 0 <= index.row() < len(self._rows):
            return None
        if role not in self._ROLES:
            return None
        return self._rows[index.row()][role - self.HeaderRole]

    def replace(self, transactions, today: date) -> None:
        """Rows for ``transactions`` (already newest first), grouped by day."""
        self.beginResetModel()
        self._rows = []
        last_day: object = ()
        for t in transactions:
            day = t.at.date() if t.at else None
            if day != last_day:
                self._rows.append((True, _day_label(day, today), "", "", False, False))
                last_day = day
            detail = " · ".join(
                part for part in (t.meal_period, _clock(t.at) if t.at else "") if part
            )
            self._rows.append(
                (False, t.activity, detail, t.amount_text, t.is_flex, t.is_deposit)
            )
        self.endResetModel()


def _day_label(day: date | None, today: date) -> str:
    """"Today" / "Yesterday" / "Mon, Sep 14". No ``%-d``: bionic lacks it."""
    if day is None:
        return "Date unknown"
    if day == today:
        return "Today"
    if day == today - timedelta(days=1):
        return "Yesterday"
    return f"{day.strftime('%a')}, {day.strftime('%b')} {day.day}"


def _clock(when: datetime) -> str:
    """"7:05 PM". ``%-I`` is a glibc extension; bionic (Android) does not have it."""
    hour = when.hour % 12 or 12
    return f"{hour}:{when.minute:02d} {'AM' if when.hour < 12 else 'PM'}"


def _count(n: int, noun: str) -> str:
    return f"{n} {noun}" if n == 1 else f"{n} {noun}s"


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

        # The one sitting the summary screen shows, picked off the clock. A
        # second model rather than a filtered view of the first: the Dining tab
        # pages through days independently, and the summary must keep showing
        # the next meal while it does.
        self._next_meal = MenuListModel(self)
        self._next_meal_on: date | None = None
        self._next_meal_block = None

        # Which sitting is next is entirely a function of the clock, so the
        # clock is a seam. Without it the test for "after dinner, show
        # tomorrow's breakfast" could only be run after dinner.
        self._now = datetime.now
        self._busy = False
        self._error = ""
        self._loaded = False

        # The meal plan is a *different* source from the menus: a server-
        # rendered Self-Service page behind the Cedarville sign-in, versus the
        # public menu API. It sits on this screen because that is where a reader
        # expects it, and it loads independently so a failure in one never
        # blanks the other.
        self._plan = MealPlan()
        self._plan_loaded = False

        # The Dining tab's history, and its one filter. Filtered here rather
        # than in QML so the list, the summary line and the empty text all
        # agree about what is being shown.
        self._activity = ActivityListModel(self)
        self._flex_only = False

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

    # ---- Meal plan ----------------------------------------------------
    # `-1` / "" are the "not reported" sentinels: QML has no null, and a
    # confident 0 or "$0.00" for an unknown balance would misstate money.

    @Property(int, notify=changed)
    def mealsRemaining(self) -> int:
        remaining = self._plan.meals_remaining
        return remaining if remaining is not None else -1

    @Property(str, notify=changed)
    def diningDollars(self) -> str:
        """The plan's own dollars ("Flex Dollars" on the page) — these **expire at the end of term**."""
        return MealPlan.money(self._plan.dining_dollars)

    @Property(str, notify=changed)
    def flexDollars(self) -> str:
        """Voluntary Flex Dollars — purchased separately, these **do not expire**."""
        return MealPlan.money(self._plan.flex_dollars)

    @Property(bool, notify=changed)
    def hasPlan(self) -> bool:
        return self._plan.has_any

    @Property(str, notify=changed)
    def mealsPeriodText(self) -> str:
        """"left this week" / "left this term", or just "left".

        The qualifier is only there when the page actually said which cycle the
        count runs on — see :attr:`MealPlan.period`.
        """
        period = self._plan.period_text
        return f"left {period}" if period else "left"

    @Property(str, notify=changed)
    def planDescription(self) -> str:
        """"21 Meals per week" / "Block 120" / "Weekly meal plan" / "".

        See :attr:`MealPlan.plan_description`.
        """
        return self._plan.plan_description

    # ---- Recent activity (the Dining tab) ----------------------------

    @Property(QObject, constant=True)
    def activity(self) -> ActivityListModel:
        return self._activity

    @Property(bool, notify=changed)
    def flexOnly(self) -> bool:
        return self._flex_only

    @Slot(bool)
    def setFlexOnly(self, on: bool) -> None:
        """A slot, not a writable property: ToggleSwitch binds ``on`` and
        reports ``clicked``, and the switch moves when this does."""
        if on != self._flex_only:
            self._flex_only = on
            self._rebuild_activity()

    @Property(str, notify=changed)
    def activitySummary(self) -> str:
        """"Since Sep 14 · 20 meals · $9.74 flex spent", or "" with nothing to sum.

        Meals are the swipes (board meals and exchanges). With the flex filter
        on, the count is of purchases instead.
        """
        shown = self._shown_activity()
        if not shown:
            return ""

        spent = sum(t.amount for t in shown if t.amount is not None and not t.is_deposit)
        added = sum(t.amount for t in shown if t.amount is not None and t.is_deposit)
        purchases = sum(1 for t in shown if t.amount is not None and not t.is_deposit)
        meals = sum(1 for t in shown if not t.is_flex)

        parts = []
        dated = [t.at for t in self._plan.transactions if t.at]
        if dated:
            oldest = min(dated)
            parts.append(f"Since {oldest.strftime('%b')} {oldest.day}")
        if self._flex_only:
            parts.append(_count(purchases, "purchase"))
            parts.append(MealPlan.money(spent))
        else:
            parts.append(_count(meals, "meal"))
            parts.append(f"{MealPlan.money(spent)} flex spent")
        if added:
            parts.append(f"+{MealPlan.money(added)} added")
        return " · ".join(parts)

    @Property(str, notify=changed)
    def activityEmptyText(self) -> str:
        """Why the list is empty, or "" when it is not."""
        if self._shown_activity():
            return ""
        if not self._plan_loaded:
            return "Sign in to see your meal plan activity."
        if self._flex_only and self._plan.transactions:
            return "No flex purchases in your recent activity."
        return "No recent activity on this card."

    def _shown_activity(self) -> tuple[MealTransaction, ...]:
        rows = self._plan.transactions
        return tuple(t for t in rows if t.is_flex) if self._flex_only else rows

    def _rebuild_activity(self) -> None:
        self._activity.replace(self._shown_activity(), self._now().date())
        self.changed.emit()

    # ---- The next sitting ---------------------------------------------
    # Public data, so this fills in before sign-in and stays filled in after
    # a sign-out — unlike everything above it.

    @Property(QObject, constant=True)
    def nextMealItems(self) -> MenuListModel:
        return self._next_meal

    @Property(bool, notify=changed)
    def hasNextMeal(self) -> bool:
        return self._next_meal_block is not None

    @Property(str, notify=changed)
    def nextMealLabel(self) -> str:
        """"Breakfast" / "Lunch" / "Dinner" — the sitting's own heading."""
        return self._next_meal_block.heading if self._next_meal_block else ""

    @Property(str, notify=changed)
    def nextMealWhen(self) -> str:
        """"Up next" when it is today, otherwise which day it is.

        After the last sitting of the day the next meal is tomorrow's
        breakfast, and calling that "up next" without saying so would have
        people turning up to a closed dining hall.
        """
        if self._next_meal_on is None:
            return ""

        today = date.today()
        if self._next_meal_on == today:
            return "Up next"
        if self._next_meal_on == today + timedelta(days=1):
            return "Tomorrow"
        return self._next_meal_on.strftime("%A")

    @Property(str, notify=changed)
    def nextMealVenue(self) -> str:
        return self._next_meal_block.venue if self._next_meal_block else HOME_COOKING

    @Slot()
    def refreshPlan(self) -> None:
        """Load the meal-plan balances. Needs the Cedarville session."""
        provider = MealsProvider(self._transport)

        def failed(exc: object) -> None:
            # Quiet on purpose: the menu is the bulk of this screen and should
            # not be replaced by an error banner because one panel is missing.
            log.warning("meal plan unavailable: %r", exc)

        run_in_background(provider.fetch, self._on_plan_loaded, failed)

    @Slot(object)
    def _on_plan_loaded(self, plan: object) -> None:
        self._plan = plan  # type: ignore[assignment]
        self._plan_loaded = True
        log.info(
            "meal plan: %s meals, dining=%s, flex=%s, %d transactions",
            self._plan.meals_remaining,
            self._plan.dining_dollars,
            self._plan.flex_dollars,
            len(self._plan.transactions),
        )
        self._rebuild_activity()

    @Slot()
    def refreshAll(self) -> None:
        """Everything on this screen: the menu *and* the meal-plan balances.

        These come from two different services with different auth, and before
        this existed only the menu was reachable from the UI — the balances
        loaded once at sign-in and then stayed on screen unchanged for the rest
        of the run, which is a bad way to show a number that goes down every
        time you eat.
        """
        self.refresh()
        self.refreshPlan()

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
        self._rebuild_next_meal()
        self.changed.emit()

    def _rebuild_next_meal(self) -> None:
        """Re-pick the sitting the summary screen shows.

        Driven off the clock, so it is recomputed on every load and every
        refresh rather than cached: an app left open over lunch should be
        showing dinner by the time you look at it again.
        """
        found = next_meal_block(self._menus, self._now())
        if found is None:
            self._next_meal_on, self._next_meal_block = None, None
            self._next_meal.replace_items(())
            return

        self._next_meal_on, self._next_meal_block = found
        self._next_meal.replace_items(self._next_meal_block.items)
        log.info(
            "next meal: %s on %s (%d items)",
            self._next_meal_block.heading,
            self._next_meal_on,
            len(self._next_meal_block.items),
        )

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
