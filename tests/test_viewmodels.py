"""Viewmodel behaviour, headless.

These touch Qt (``QT_QPA_PLATFORM=offscreen`` is set in ``conftest.py``) but
never start an event loop or a thread: the completion handlers are invoked
directly, which is what makes the tests deterministic. The threading itself is
one line in ``mycu/ui/tasks.py`` and is exercised by running the app.
"""

from __future__ import annotations

from datetime import date, datetime, time, timedelta
from pathlib import Path

import pytest

pytest.importorskip("PySide6.QtCore", reason="PySide6 not available")

from mycu.core.errors import ParseError, SessionExpired, TransportError  # noqa: E402
from mycu.core.models import (  # noqa: E402
    HOME_COOKING,
    AllowanceLine,
    ChapelLedgerEntry,
    ChapelSummary,
    DayMenu,
    MealPlan,
    MealTransaction,
    MenuBlock,
    MenuItem,
    UpcomingChapel,
)
from mycu.core.session import SessionStore  # noqa: E402
from mycu.core.transport import FixtureTransport  # noqa: E402
from mycu.ui.viewmodels.chapel import ChapelListModel, ChapelViewModel  # noqa: E402
from mycu.ui.viewmodels.dining import (  # noqa: E402
    ActivityListModel,
    DiningViewModel,
    MenuListModel,
)


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


# ---------------------------------------------------------------------------
# What the summary screen reads
# ---------------------------------------------------------------------------

def test_the_skip_bar_is_a_fraction_of_the_allowance(vm: ChapelViewModel) -> None:
    vm._on_loaded(ChapelSummary(used=2, total=18, remaining=16))
    assert vm.remainingFraction == pytest.approx(16 / 18)


def test_the_bar_is_empty_rather_than_wrong_when_the_figures_are_missing(
    vm: ChapelViewModel,
) -> None:
    vm._on_loaded(ChapelSummary())
    assert vm.remainingFraction == 0.0


def test_the_bar_never_overflows_its_track(vm: ChapelViewModel) -> None:
    """The server's two halves of arithmetic are not guaranteed to agree.

    See ChapelSummary: `used` and `remaining` come from different parts of
    Cedarville's own sums. A bar past the end of its track looks broken in a
    way a full bar does not.
    """
    vm._on_loaded(ChapelSummary(used=0, total=4, remaining=9))
    assert vm.remainingFraction == 1.0


def test_the_day_badge_and_the_date_line_are_separate(vm: ChapelViewModel) -> None:
    """Two pieces of text on the card, so two properties.

    Slicing the badge back out of a formatted "Tomorrow 10:00 AM" is how a UI
    ends up rendering a time inside a pill.
    """
    when = datetime.combine(date.today() + timedelta(days=1), time(10, 0))
    vm._next = UpcomingChapel(starts_at=when, title="Worship Chapel")

    assert vm.nextChapelDay == "Tomorrow"
    assert vm.nextChapelDateText.endswith("10:00 AM")
    assert "Tomorrow" not in vm.nextChapelDateText


def test_today_and_a_weekday_are_both_spelled_out(vm: ChapelViewModel) -> None:
    vm._next = UpcomingChapel(starts_at=datetime.combine(date.today(), time(10, 0)))
    assert vm.nextChapelDay == "Today"

    later = date.today() + timedelta(days=4)
    vm._next = UpcomingChapel(starts_at=datetime.combine(later, time(10, 0)))
    assert vm.nextChapelDay == later.strftime("%A")


def test_no_upcoming_chapel_renders_as_nothing_at_all(vm: ChapelViewModel) -> None:
    """Over the summer there genuinely is no next chapel, and "TBA" is a claim."""
    assert vm.nextChapelDay == ""
    assert vm.nextChapelDateText == ""


def test_midnight_and_noon_are_not_rendered_as_zero_and_twelve(vm: ChapelViewModel) -> None:
    """`%-I` is a glibc extension bionic does not have, so the hour is done by hand."""
    vm._next = UpcomingChapel(starts_at=datetime.combine(date.today(), time(0, 5)))
    assert "12:05 AM" in vm.nextChapelDateText

    vm._next = UpcomingChapel(starts_at=datetime.combine(date.today(), time(12, 0)))
    assert "12:00 PM" in vm.nextChapelDateText


# ---------------------------------------------------------------------------
# The schedule (the Chapel tab)
# ---------------------------------------------------------------------------

def _local(y: int, mo: int, d: int, h: int = 10, mi: int = 0) -> datetime:
    return datetime(y, mo, d, h, mi).astimezone()


#: A Wednesday, ten minutes into chapel.
NOW = _local(2026, 9, 23, 10, 10)

SCHEDULE = (
    UpcomingChapel(starts_at=_local(2026, 9, 22), title="Garrett Kell",
                   speakers=("Garrett Kell",)),
    UpcomingChapel(starts_at=_local(2026, 9, 23), title="Garrett Kell",
                   speakers=("Garrett Kell",), description="Lead pastor of Del Ray.",
                   will_livestream=True),
    UpcomingChapel(starts_at=_local(2026, 9, 24), title="SGA", will_livestream=True),
    UpcomingChapel(starts_at=_local(2026, 9, 28), title="Sermon on the Mount",
                   speakers=("Philip Miller",), will_livestream=True),
    UpcomingChapel(starts_at=_local(2026, 10, 5, 11), title="Majors Assembly"),
    UpcomingChapel(starts_at=None, title="Undated"),
)


def _schedule_rows(vm: ChapelViewModel, *roles: str) -> list[tuple]:
    model = vm.schedule
    by_name = {bytes(name).decode(): role for role, name in model.roleNames().items()}
    return [
        tuple(model.data(model.index(r, 0), by_name[role]) for role in roles)
        for r in range(model.rowCount())
    ]


@pytest.fixture
def svm(vm: ChapelViewModel) -> ChapelViewModel:
    vm._now = lambda: NOW
    vm._on_schedule_loaded(SCHEDULE)
    return vm


def test_the_schedule_is_grouped_by_week(svm: ChapelViewModel) -> None:
    assert _schedule_rows(svm, "isHeader", "heading", "who") == [
        (True, "This week", ""),
        (False, "", "Garrett Kell"),
        (False, "", "SGA"),
        (True, "Next week", ""),
        (False, "", "Philip Miller"),
        (True, "Week of Oct 5", ""),
        (False, "", "Majors Assembly"),
    ]


def test_a_finished_chapel_is_gone_and_the_current_one_is_marked_now(
    svm: ChapelViewModel,
) -> None:
    rows = _schedule_rows(svm, "isHeader", "dayName", "dayNumber", "badge", "isNow")
    chapels = [r[1:] for r in rows if not r[0]]
    assert chapels[0] == ("WED", "23", "Now", True)
    assert chapels[1] == ("THU", "24", "Tomorrow", False)
    assert chapels[2][2:] == ("", False)


def test_the_title_is_shown_only_when_it_adds_something(svm: ChapelViewModel) -> None:
    subtitles = [r[1] for r in _schedule_rows(svm, "isHeader", "subtitle") if not r[0]]
    # Same as the speaker; an unnamed chapel whose title *is* its name; a real
    # sermon title; an unnamed assembly.
    assert subtitles == ["", "", "Sermon on the Mount", ""]


def test_time_description_and_livestream_come_through(svm: ChapelViewModel) -> None:
    rows = [r[1:] for r in _schedule_rows(svm, "isHeader", "timeText", "description",
                                          "livestream") if not r[0]]
    assert rows[0] == ("10:00 AM", "Lead pastor of Del Ray.", True)
    assert rows[-1] == ("11:00 AM", "", False)


def test_the_summary_still_gets_the_next_chapel_from_the_full_schedule(
    vm: ChapelViewModel,
) -> None:
    now = datetime.now().astimezone()
    # Soonest first, as fetch_schedule delivers it. The first has started, so
    # it is on the Chapel tab as "Now" but is not the summary's *next* chapel.
    vm._on_schedule_loaded((
        UpcomingChapel(starts_at=now - timedelta(minutes=5), title="Started",
                       speakers=("Started Speaker",)),
        UpcomingChapel(starts_at=now + timedelta(days=1), title="Sooner",
                       speakers=("Sooner Speaker",)),
    ))
    assert vm.nextSpeaker == "Sooner Speaker"


def test_why_the_schedule_is_empty(vm: ChapelViewModel) -> None:
    assert vm.scheduleEmptyText == "Loading the chapel schedule…"

    vm._on_schedule_failed(TransportError("timed out"))
    assert vm.scheduleEmptyText.startswith("Couldn't reach the chapel schedule")

    vm._on_schedule_loaded(())
    assert vm.scheduleEmptyText == "No chapels scheduled right now."

    vm._on_schedule_failed(ParseError("no Items"))
    assert vm.scheduleEmptyText == "The chapel schedule feed changed shape."


def test_a_populated_schedule_has_no_empty_text(svm: ChapelViewModel) -> None:
    assert svm.scheduleEmptyText == ""


# ---------------------------------------------------------------------------
# Dining viewmodel
# ---------------------------------------------------------------------------

@pytest.fixture
def dvm(fixtures_dir: Path) -> DiningViewModel:
    vm = DiningViewModel(FixtureTransport(fixtures_dir))
    # Pin the clock to breakfast time. Which sitting is "next" is a function of
    # the hour, and a test that only passes before 10:30am is not a test.
    vm._now = lambda: datetime.combine(date.today(), time(7, 0))
    return vm


def test_the_meals_qualifier_follows_the_page(dvm: DiningViewModel) -> None:
    dvm._plan = MealPlan(meals_remaining=19, period="week")
    assert dvm.mealsPeriodText == "left this week"
    assert dvm.planDescription == "Weekly meal plan"


def test_an_unknown_cycle_drops_the_qualifier_rather_than_inventing_one(
    dvm: DiningViewModel,
) -> None:
    dvm._plan = MealPlan(meals_remaining=19)
    assert dvm.mealsPeriodText == "left"
    assert dvm.planDescription == ""


def test_the_next_sitting_is_exposed_without_a_heading_row(dvm: DiningViewModel) -> None:
    """The card's own header already names the meal; the list must not repeat it."""
    dvm._on_loaded((
        DayMenu(on=date.today(), blocks=(
            MenuBlock(venue=HOME_COOKING, meal="Breakfast", slot="breakfast", items=(
                MenuItem(name="Bacon"),
                MenuItem(name="Biscuits & Country Gravy", allergens=("gluten", "dairy")),
            )),
        )),
    ))

    model = dvm.nextMealItems
    assert model.rowCount() == 2
    assert all(
        model.data(model.index(row, 0), MenuListModel.HeaderRole) is False
        for row in range(model.rowCount())
    )
    assert model.data(model.index(1, 0), MenuListModel.AllergenRole) == "gluten, dairy"


def test_paging_the_dining_tab_does_not_move_the_summary_card(dvm: DiningViewModel) -> None:
    """The two are different questions and must not share a model."""
    dvm._on_loaded((
        DayMenu(on=date.today(), blocks=(
            MenuBlock(venue=HOME_COOKING, meal="Dinner", slot="dinner",
                      items=(MenuItem(name="Bratwurst"),)),
        )),
        DayMenu(on=date.today() + timedelta(days=1), blocks=(
            MenuBlock(venue=HOME_COOKING, meal="Breakfast", slot="breakfast",
                      items=(MenuItem(name="Bacon"),)),
        )),
    ))
    before = dvm.nextMealLabel

    dvm.nextDay()

    assert dvm.dayOffset == 1
    assert dvm.nextMealLabel == before


def test_no_menu_at_all_is_reported_as_no_menu(dvm: DiningViewModel) -> None:
    dvm._on_loaded(())
    assert dvm.hasNextMeal is False
    assert dvm.nextMealLabel == ""
    assert dvm.nextMealItems.rowCount() == 0
    # Still names the station, so the card has a title while it is empty.
    assert dvm.nextMealVenue == HOME_COOKING


def test_tomorrows_breakfast_says_so(dvm: DiningViewModel) -> None:
    """Calling it "up next" would have people turning up to a closed hall."""
    dvm._on_loaded((
        DayMenu(on=date.today() + timedelta(days=1), blocks=(
            MenuBlock(venue=HOME_COOKING, meal="Breakfast", slot="breakfast",
                      items=(MenuItem(name="Bacon"),)),
        )),
    ))
    assert dvm.nextMealWhen == "Tomorrow"
    assert dvm.nextMealLabel == "Breakfast"


# ---------------------------------------------------------------------------
# Paging through days (the Chucks tab)
# ---------------------------------------------------------------------------

@pytest.fixture
def fetches(monkeypatch: pytest.MonkeyPatch) -> list:
    """Every background fetch the dining viewmodel starts, never run.

    Each entry is ``(provider, on_done, on_error)``; a test answers one by
    calling its handler, the same way the rest of this file calls ``_on_loaded``.
    """
    calls: list = []

    def capture(fn, on_done, on_error) -> None:
        calls.append((fn.__self__, on_done, on_error))

    monkeypatch.setattr("mycu.ui.viewmodels.dining.run_in_background", capture)
    return calls


def _home(on: date, *dishes: str) -> DayMenu:
    return DayMenu(on=on, blocks=(
        MenuBlock(venue=HOME_COOKING, meal="Lunch", slot="lunch",
                  items=tuple(MenuItem(name=d) for d in dishes)),
    ))


def _texts(vm: DiningViewModel) -> list[str]:
    model = vm.items
    return [model.data(model.index(r, 0), MenuListModel.TextRole)
            for r in range(model.rowCount())]


TODAY = date.today()


def test_paging_back_fetches_the_week_ending_on_that_day(
    dvm: DiningViewModel, fetches: list,
) -> None:
    dvm._on_loaded((_home(TODAY, "Bratwurst"),))
    assert fetches == []

    dvm.previousDay()

    assert dvm.dayOffset == -1
    assert dvm.dateText == "Yesterday"
    assert dvm.dayLoading is True
    assert dvm.dayEmptyText == "Loading the menu…"
    (provider, done, _), = fetches
    assert provider.start == TODAY - timedelta(days=7)
    assert provider.days == 7


def test_a_fetched_window_fills_the_day_and_serves_the_rest_of_the_week(
    dvm: DiningViewModel, fetches: list,
) -> None:
    dvm._on_loaded((_home(TODAY, "Bratwurst"),))
    dvm.previousDay()
    _, done, _ = fetches[0]

    done((_home(TODAY - timedelta(days=1), "Tacos"),
          _home(TODAY - timedelta(days=2), "Lasagna")))

    assert _texts(dvm) == ["Lunch", "Tacos"]
    assert dvm.dayLoading is False
    dvm.previousDay()
    assert _texts(dvm) == ["Lunch", "Lasagna"]
    assert len(fetches) == 1


def test_paging_forward_past_the_week_fetches_from_that_day(
    dvm: DiningViewModel, fetches: list,
) -> None:
    dvm._on_loaded((_home(TODAY, "Bratwurst"),))
    for _ in range(7):
        dvm.nextDay()

    (provider, _, _), = fetches
    assert provider.start == TODAY + timedelta(days=7)


def test_a_day_with_nothing_posted_says_so(dvm: DiningViewModel, fetches: list) -> None:
    dvm._on_loaded((DayMenu(on=TODAY, blocks=(
        MenuBlock(venue="No Venues Found", meal="", slot="anytime", items=()),
    )),))
    assert dvm.items.rowCount() == 0
    assert dvm.dayEmptyText == "Nothing posted for Home Cooking on this day."
    assert fetches == []


def test_a_failed_window_is_reported_on_its_own_day_only(
    dvm: DiningViewModel, fetches: list,
) -> None:
    dvm._on_loaded((_home(TODAY, "Bratwurst"),))
    dvm.previousDay()
    _, _, failed = fetches[0]

    failed(TransportError("timed out"))

    assert "Couldn't reach the dining menu service" in dvm.dayEmptyText
    assert dvm.error == ""  # the summary card's menu is unaffected

    dvm.goToToday()
    assert dvm.isToday is True
    assert _texts(dvm) == ["Lunch", "Bratwurst"]
    assert dvm.dayEmptyText == ""


def test_going_back_to_a_failed_day_tries_again(dvm: DiningViewModel, fetches: list) -> None:
    dvm._on_loaded((_home(TODAY, "Bratwurst"),))
    dvm.previousDay()
    fetches[0][2](TransportError("timed out"))

    dvm.nextDay()
    dvm.previousDay()

    assert len(fetches) == 2
    assert dvm.dayEmptyText == "Loading the menu…"


def test_a_refresh_refetches_a_paged_day_rather_than_keeping_it(
    dvm: DiningViewModel, fetches: list,
) -> None:
    dvm._on_loaded((_home(TODAY, "Bratwurst"),))
    dvm.previousDay()
    fetches[0][1]((_home(TODAY - timedelta(days=1), "Tacos"),))

    dvm._on_loaded((_home(TODAY, "Bratwurst"),))

    assert len(fetches) == 2
    assert fetches[1][0].start == TODAY - timedelta(days=1)


def test_a_distant_day_names_its_year() -> None:
    vm = DiningViewModel(None)
    vm._offset = (date(TODAY.year - 1, 9, 1) - TODAY).days
    assert vm.dateDetail.endswith(f", {TODAY.year - 1}")
    assert vm.dateDetail.startswith(date(TODAY.year - 1, 9, 1).strftime("%A, September 1"))


# ---------------------------------------------------------------------------
# Recent activity (the Dining tab)
# ---------------------------------------------------------------------------

def _at(days_ago: int, hour: int, minute: int = 0) -> datetime:
    return datetime.combine(date.today() - timedelta(days=days_ago), time(hour, minute))


ACTIVITY = (
    MealTransaction(at=_at(0, 12, 25), activity="Board meal", meal_period="Lunch"),
    MealTransaction(at=_at(0, 0, 5), activity="Flex purchase", amount=3.74),
    MealTransaction(at=_at(1, 17, 45), activity="Meal exchange", meal_period="Dinner"),
    MealTransaction(at=_at(1, 12, 0), activity="Flex purchase", meal_period="Lunch",
                    amount=6.0),
    MealTransaction(at=_at(3, 7, 40), activity="Board meal", meal_period="Breakfast"),
)


def _rows(vm: DiningViewModel) -> list[tuple]:
    model = vm.activity
    roles = (ActivityListModel.HeaderRole, ActivityListModel.TitleRole,
             ActivityListModel.DetailRole, ActivityListModel.AmountRole)
    return [
        tuple(model.data(model.index(r, 0), role) for role in roles)
        for r in range(model.rowCount())
    ]


def test_activity_is_grouped_by_day_under_readable_headers(dvm: DiningViewModel) -> None:
    dvm._on_plan_loaded(MealPlan(meals_remaining=16, transactions=ACTIVITY))
    three_days_ago = date.today() - timedelta(days=3)

    assert _rows(dvm) == [
        (True, "Today", "", ""),
        (False, "Board meal", "Lunch · 12:25 PM", ""),
        (False, "Flex purchase", "12:05 AM", "\u2212$3.74"),
        (True, "Yesterday", "", ""),
        (False, "Meal exchange", "Dinner · 5:45 PM", ""),
        (False, "Flex purchase", "Lunch · 12:00 PM", "\u2212$6.00"),
        (True, f"{three_days_ago.strftime('%a, %b')} {three_days_ago.day}", "", ""),
        (False, "Board meal", "Breakfast · 7:40 AM", ""),
    ]
    assert dvm.activitySummary == (
        f"Since {three_days_ago.strftime('%b')} {three_days_ago.day}"
        " · 3 meals · $9.74 flex spent"
    )
    assert dvm.activityEmptyText == ""


def test_the_flex_toggle_shows_only_money(dvm: DiningViewModel) -> None:
    dvm._on_plan_loaded(MealPlan(meals_remaining=16, transactions=ACTIVITY))

    dvm.setFlexOnly(True)

    assert dvm.flexOnly is True
    assert [row[1] for row in _rows(dvm) if not row[0]] == ["Flex purchase"] * 2
    assert dvm.activitySummary.endswith(" · 2 purchases · $9.74")

    dvm.setFlexOnly(False)
    assert len([row for row in _rows(dvm) if not row[0]]) == len(ACTIVITY)


def test_a_deposit_is_summed_separately_from_spending(dvm: DiningViewModel) -> None:
    dvm._on_plan_loaded(MealPlan(transactions=(
        MealTransaction(at=_at(0, 9), activity="Deposit", amount=20.0, is_deposit=True),
        MealTransaction(at=_at(0, 8), activity="Flex purchase", amount=3.0),
    )))
    dvm.setFlexOnly(True)
    assert dvm.activitySummary.endswith(" · 1 purchase · $3.00 · +$20.00 added")


def test_why_the_activity_list_is_empty(dvm: DiningViewModel) -> None:
    assert dvm.activityEmptyText == "Sign in to see your meal plan activity."
    assert dvm.activitySummary == ""

    dvm._on_plan_loaded(MealPlan(meals_remaining=16))
    assert dvm.activityEmptyText == "No recent activity on this card."

    dvm._on_plan_loaded(MealPlan(transactions=ACTIVITY[:1]))
    dvm.setFlexOnly(True)
    assert dvm.activity.rowCount() == 0
    assert dvm.activityEmptyText == "No flex purchases in your recent activity."


def test_the_fixture_activity_loads_end_to_end(dvm: DiningViewModel, fixtures_dir: Path) -> None:
    from mycu.core.providers.meals import MealsProvider

    dvm._on_plan_loaded(MealsProvider(FixtureTransport(fixtures_dir)).fetch())
    assert dvm.activity.rowCount() > 19  # every row, plus a header per day
    assert dvm.activitySummary == "Since Dec 27 · 16 meals · $13.99 flex spent"

