"""Dining menus, against a REAL captured response.

Unlike `test_chapel_provider.py`, these assertions are not built on guesses.
`tests/fixtures/diningdata_cedarville_edu_api_menus.json` is a verbatim capture
of `https://diningdata.cedarville.edu/api/menus?days=2` taken on 2026-09-16, so
a failure here means either the code broke or Cedarville changed the API — not
that a placeholder needs updating.

The fixture contains no personal data. The endpoint is unauthenticated: the
dining site's own script fetches it with `credentials: "omit"`.
"""

from __future__ import annotations

import json
from datetime import date, datetime
from pathlib import Path

import pytest

from mycu.core.errors import ParseError
from mycu.core.models import HOME_COOKING, SLOT_ORDER, DayMenu, MenuBlock, MenuItem
from mycu.core.providers.dining import (
    DINING_PATH,
    DiningProvider,
    home_cooking_for,
    next_meal_block,
    parse_menus,
)
from mycu.core.transport import DINING_BASE, FixtureTransport

FIXTURE_DATE = date(2026, 9, 16)


@pytest.fixture
def menus(fixtures_dir: Path) -> tuple[DayMenu, ...]:
    raw = (fixtures_dir / "diningdata_cedarville_edu_api_menus.json").read_text()
    return parse_menus(json.loads(raw))


# ---------------------------------------------------------------------------
# Shape
# ---------------------------------------------------------------------------

def test_the_capture_covers_two_days(menus) -> None:
    assert len(menus) == 2
    assert menus[0].on == FIXTURE_DATE
    assert menus[1].on == date(2026, 9, 17)


def test_days_come_back_in_date_order(menus) -> None:
    # Dict ordering is a JSON serialisation detail; a UI must not depend on it.
    assert [d.on for d in menus] == sorted(d.on for d in menus)


def test_each_day_has_the_full_set_of_stations(menus) -> None:
    assert len(menus[0].blocks) == 20
    assert HOME_COOKING in menus[0].venues


# ---------------------------------------------------------------------------
# Home Cooking — the thing the screen is actually for
# ---------------------------------------------------------------------------

def test_home_cooking_has_all_three_meals(menus) -> None:
    blocks = menus[0].for_venue(HOME_COOKING)
    assert [b.meal for b in blocks] == ["Breakfast", "Lunch", "Dinner"]


def test_meals_are_ordered_chronologically_not_as_received(menus) -> None:
    """Dinner listed before breakfast reads as a bug even when it's accurate."""
    scrambled = DayMenu(
        on=FIXTURE_DATE,
        blocks=(
            MenuBlock(venue=HOME_COOKING, meal="Dinner", slot="dinner"),
            MenuBlock(venue=HOME_COOKING, meal="Breakfast", slot="breakfast"),
            MenuBlock(venue=HOME_COOKING, meal="Lunch", slot="lunch"),
        ),
    )
    assert [b.meal for b in scrambled.for_venue()] == ["Breakfast", "Lunch", "Dinner"]


def test_ordering_keys_on_slot_not_on_the_free_text_meal_label(menus) -> None:
    """Regression, found against the real payload.

    `meal` is not a sitting. In the live data it is sometimes null (Grille,
    Italian), sometimes the sitting ("Breakfast"), and sometimes a sub-station
    name — "yogurt bar" under Breakfast All Day, "Deli" under Allergen Aware.
    Sorting on it pushed "yogurt bar", a breakfast block, past dinner.
    """
    yogurt = MenuBlock(venue="Breakfast All Day", meal="yogurt bar", slot="breakfast")
    dinner = MenuBlock(venue=HOME_COOKING, meal="Dinner", slot="dinner")
    all_day = MenuBlock(venue="Grille", meal="", slot="anytime")

    assert yogurt.sort_key < dinner.sort_key < all_day.sort_key


def test_heading_falls_back_to_the_slot_when_meal_is_missing() -> None:
    assert MenuBlock(venue="Grille", meal="", slot="anytime").heading == "All day"
    assert MenuBlock(venue="X", meal="", slot="lunch").heading == "Lunch"
    # …but a more specific upstream label wins.
    assert MenuBlock(venue="X", meal="Deli", slot="anytime").heading == "Deli"


def test_real_dishes_come_through(menus) -> None:
    breakfast = menus[0].for_venue(HOME_COOKING)[0]
    names = [i.name for i in breakfast.items]
    assert "Chorizo Sausage Patties" in names
    assert "Mexican Potatoes" in names


def test_allergen_icons_become_plain_labels(menus) -> None:
    """The API sends {url, alt} icon objects; only the alt text is useful."""
    breakfast = menus[0].for_venue(HOME_COOKING)[0]
    cheese = next(i for i in breakfast.items if i.name == "Shredded Cheese")

    assert cheese.allergens == ("dairy",)
    assert cheese.allergen_text == "dairy"
    assert all(isinstance(a, str) for a in cheese.allergens)


def test_items_with_no_allergens_are_empty_not_none(menus) -> None:
    breakfast = menus[0].for_venue(HOME_COOKING)[0]
    plain = next(i for i in breakfast.items if i.name == "Chorizo Sausage Patties")
    assert plain.allergens == ()
    assert plain.allergen_text == ""


def test_multiple_allergens_are_all_kept(menus) -> None:
    lunch_and_dinner = menus[1].for_venue(HOME_COOKING)
    gravy = next(
        i
        for b in lunch_and_dinner
        for i in b.items
        if i.name == "Biscuits & Country Gravy"
    )
    assert set(gravy.allergens) == {"gluten", "dairy", "egg", "soy"}


def test_doubled_spaces_in_upstream_names_are_normalised(menus) -> None:
    """The real payload contains "Eggs with Peppers and  Onion" — two spaces."""
    breakfast = menus[0].for_venue(HOME_COOKING)[0]
    assert "Eggs with Peppers and Onion" in [i.name for i in breakfast.items]


def test_home_cooking_for_picks_the_right_day(menus) -> None:
    blocks = home_cooking_for(menus, FIXTURE_DATE)
    assert [b.meal for b in blocks] == ["Breakfast", "Lunch", "Dinner"]


def test_home_cooking_for_a_missing_day_is_empty_not_an_error(menus) -> None:
    # The API only serves forward from today, so asking about yesterday
    # legitimately returns nothing. That is not a failure.
    assert home_cooking_for(menus, date(2020, 1, 1)) == ()


def test_venue_lookup_is_case_insensitive(menus) -> None:
    assert menus[0].for_venue("home cooking") == menus[0].for_venue(HOME_COOKING)


# ---------------------------------------------------------------------------
# All-day stations
# ---------------------------------------------------------------------------

def test_all_day_stations_sort_last(menus) -> None:
    """Grille, Italian, SubZone etc. carry slot: "anytime"."""
    anytime = [b for b in menus[0].blocks if b.slot == "anytime"]
    assert anytime, "the capture should contain all-day stations"
    assert all(b.sort_key == len(SLOT_ORDER) for b in anytime)
    # Most, but NOT all, of them have a null meal — "Deli" under Allergen Aware
    # is an all-day block with a label. That is why `meal` cannot drive sorting.
    assert any(b.meal == "" for b in anytime)
    assert any(b.meal != "" for b in anytime)


def test_every_block_has_one_of_the_four_known_slots(menus) -> None:
    slots = {b.slot for day in menus for b in day.blocks}
    assert slots <= {"breakfast", "lunch", "dinner", "anytime"}, slots


def test_null_meal_becomes_empty_string_not_none(menus) -> None:
    # So neither the model nor the QML ever has to think about None.
    assert all(isinstance(b.meal, str) for b in menus[0].blocks)


# ---------------------------------------------------------------------------
# Robustness
# ---------------------------------------------------------------------------

def test_a_malformed_block_is_skipped_not_fatal() -> None:
    """A menu missing one station is still a useful menu."""
    days = parse_menus({
        "2026-09-16": [
            "this is not a block",
            {"no_venue": True},
            {"venue": "Home Cooking", "meal": "Lunch", "slot": "lunch",
             "items": [{"name": "Soup"}, "not an item", {"no_name": 1}]},
        ]
    })
    assert len(days) == 1
    assert len(days[0].blocks) == 1
    assert [i.name for i in days[0].blocks[0].items] == ["Soup"]


def test_an_unparseable_date_key_is_skipped() -> None:
    days = parse_menus({
        "not-a-date": [{"venue": "Grille"}],
        "2026-09-16": [{"venue": "Home Cooking"}],
    })
    assert [d.on for d in days] == [date(2026, 9, 16)]


def test_a_wholly_unusable_payload_raises() -> None:
    with pytest.raises(ParseError):
        parse_menus({"not-a-date": []})


def test_a_list_payload_raises_rather_than_returning_nothing() -> None:
    with pytest.raises(ParseError, match="date"):
        parse_menus([{"venue": "Home Cooking"}])


def test_legacy_tags_key_still_works() -> None:
    """The dining site's own menu.js still reads `tags`; the API sends
    `allergens`. Accepting both costs one line and future-proofs a rename."""
    days = parse_menus({
        "2026-09-16": [
            {"venue": "Home Cooking", "meal": "Lunch",
             "items": [{"name": "Pizza", "tags": ["gluten", "dairy"]}]}
        ]
    })
    assert days[0].blocks[0].items[0].allergens == ("gluten", "dairy")


def test_duplicate_allergens_are_collapsed() -> None:
    days = parse_menus({
        "2026-09-16": [
            {"venue": "X", "items": [{"name": "Y", "allergens": [
                {"alt": "dairy"}, {"alt": "dairy"}, {"alt": "soy"}]}]}
        ]
    })
    assert days[0].blocks[0].items[0].allergens == ("dairy", "soy")


# ---------------------------------------------------------------------------
# The provider
# ---------------------------------------------------------------------------

def test_provider_targets_the_dining_origin_not_selfservice() -> None:
    """This is what makes TransportRouter send it over plain HTTP."""
    assert DiningProvider(None).path.startswith(DINING_BASE)
    assert DINING_PATH == f"{DINING_BASE}/api/menus"


def test_provider_requests_the_days_it_was_asked_for() -> None:
    assert DiningProvider(None, days=3).path.endswith("?days=3")


def test_days_is_clamped_to_something_sane() -> None:
    assert DiningProvider(None, days=0).days == 1
    assert DiningProvider(None, days=-5).days == 1


def test_provider_never_sends_refresh() -> None:
    """`refresh=1` forces an upstream refetch from Pioneer College Caterers.

    A personal app has no business making someone else's server work harder.
    """
    assert "refresh" not in DiningProvider(None, days=7).path


def test_provider_end_to_end_over_the_fixture(fixtures_dir: Path) -> None:
    days = DiningProvider(FixtureTransport(fixtures_dir)).fetch()
    assert len(days) == 2
    assert days[0].for_venue(HOME_COOKING)[0].items


def test_fixture_slug_includes_the_host_for_non_default_origins() -> None:
    # Otherwise a /api/menus fixture would collide with a Self-Service one.
    assert (
        FixtureTransport.slug(f"{DINING_BASE}/api/menus?days=2")
        == "diningdata_cedarville_edu_api_menus"
    )


# ---------------------------------------------------------------------------
# The next sitting
#
# What the summary screen puts on the front page. The menu feed carries no
# serving times, so the cutoffs in `SERVING_ENDS` are the app's own — which is
# exactly why they need tests: a rule nobody can look up is a rule that drifts.
# ---------------------------------------------------------------------------

def test_before_half_ten_the_next_meal_is_breakfast(menus: tuple[DayMenu, ...]) -> None:
    on, block = next_meal_block(menus, datetime(2026, 9, 16, 7, 30))
    assert on == FIXTURE_DATE
    assert block.slot == "breakfast"


def test_late_morning_has_moved_on_to_lunch(menus: tuple[DayMenu, ...]) -> None:
    _on, block = next_meal_block(menus, datetime(2026, 9, 16, 10, 45))
    assert block.slot == "lunch"


def test_the_afternoon_is_looking_at_dinner(menus: tuple[DayMenu, ...]) -> None:
    _on, block = next_meal_block(menus, datetime(2026, 9, 16, 16, 30))
    assert block.slot == "dinner"


def test_after_dinner_it_rolls_over_to_tomorrow(menus: tuple[DayMenu, ...]) -> None:
    """The alternative is showing a menu for a meal that is already over."""
    on, block = next_meal_block(menus, datetime(2026, 9, 16, 21, 0))
    assert on == date(2026, 9, 17)
    assert block.slot == "breakfast"


def test_the_boundary_belongs_to_the_meal_that_is_ending(menus: tuple[DayMenu, ...]) -> None:
    assert next_meal_block(menus, datetime(2026, 9, 16, 10, 29))[1].slot == "breakfast"
    assert next_meal_block(menus, datetime(2026, 9, 16, 10, 30))[1].slot == "lunch"


def test_nothing_left_in_the_payload_is_not_an_error(menus: tuple[DayMenu, ...]) -> None:
    """The API serves forward from today, so this happens with a stale cache."""
    assert next_meal_block(menus, datetime(2026, 9, 20, 7, 0)) is None


def test_all_day_stations_are_never_up_next() -> None:
    """"Breakfast All Day" is always on; it is not a sitting you can be before."""
    day = DayMenu(
        on=date(2026, 9, 16),
        blocks=(
            MenuBlock(venue=HOME_COOKING, meal="yogurt bar", slot="anytime",
                      items=(MenuItem(name="Granola"),)),
            MenuBlock(venue=HOME_COOKING, meal="Lunch", slot="lunch",
                      items=(MenuItem(name="Pork Loin"),)),
        ),
    )
    _on, block = next_meal_block((day,), datetime(2026, 9, 16, 7, 0))
    assert block.slot == "lunch"


def test_an_empty_sitting_is_skipped_for_the_next_real_one() -> None:
    """A heading with no dishes under it reads as a bug, not as a menu."""
    day = DayMenu(
        on=date(2026, 9, 16),
        blocks=(
            MenuBlock(venue=HOME_COOKING, meal="Breakfast", slot="breakfast", items=()),
            MenuBlock(venue=HOME_COOKING, meal="Lunch", slot="lunch",
                      items=(MenuItem(name="Pork Loin"),)),
        ),
    )
    _on, block = next_meal_block((day,), datetime(2026, 9, 16, 7, 0))
    assert block.slot == "lunch"


def test_only_the_asked_for_station_is_considered(menus: tuple[DayMenu, ...]) -> None:
    _on, block = next_meal_block(menus, datetime(2026, 9, 16, 7, 30))
    assert block.venue == HOME_COOKING
