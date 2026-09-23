"""The QML↔viewmodel contract.

QML is resolved at runtime, so ``chapel.refrehAll()`` is not a syntax error —
it is a silent no-op on desktop and a line in logcat on the phone, found only
by pressing the button. The viewmodels are exposed to QML as context
properties, which is the loosest binding Qt offers and the one with no
compile-time checking at all.

So: every ``chapel.<name>`` and ``dining.<name>`` written in the QML must
resolve to a real ``Property`` or ``Slot`` on the corresponding viewmodel.
Read off the Qt metaobject rather than from ``dir()``, because that is exactly
what QML itself looks at — a plain Python method on a QObject is invisible to
QML unless it is decorated, and this test has to fail in that case too.
"""

from __future__ import annotations

import re
from pathlib import Path

import pytest

pytest.importorskip("PySide6.QtCore", reason="PySide6 not available")

from mycu.ui.settings import SettingsController  # noqa: E402
from mycu.ui.viewmodels.chapel import ChapelViewModel  # noqa: E402
from mycu.ui.viewmodels.dining import DiningViewModel  # noqa: E402
from mycu.ui.viewmodels.semester import SemesterViewModel  # noqa: E402

QML_DIR = Path(__file__).resolve().parents[1] / "mycu" / "ui" / "qml"

#: The context-property name each viewmodel is published under in ``app.py``.
VIEWMODELS = {
    "chapel": ChapelViewModel,
    "dining": DiningViewModel,
    "semester": SemesterViewModel,
}

#: Everything QML binds to by context-property name. ``settings`` is not a
#: viewmodel — it has no provider, no busy flag and no ``refreshAll`` — but it
#: is bound the same loose way, and by *every* Theme.qml instance, so a typo
#: there is the whole app stuck on one palette rather than one broken screen.
BOUND_OBJECTS = {**VIEWMODELS, "settings": SettingsController}

COMMENT_RE = re.compile(r"//[^\n]*")


def exposed_names(cls: type) -> set[str]:
    """Property and invokable-method names QML can actually see on ``cls``."""
    meta = cls.staticMetaObject
    names = {meta.property(i).name() for i in range(meta.propertyCount())}
    names |= {
        bytes(meta.method(i).name()).decode() for i in range(meta.methodCount())
    }
    return names


def references(source: str) -> set[tuple[str, str]]:
    """``(object, member)`` pairs used in a QML file, comments removed.

    Comments are stripped because both view files name their viewmodel's Python
    class in a header comment (``…viewmodels.dining.DiningViewModel``), which
    would otherwise read as a reference to a member called ``DiningViewModel``.
    """
    stripped = COMMENT_RE.sub("", source)
    pattern = re.compile(rf"\b({'|'.join(BOUND_OBJECTS)})\.(\w+)")
    return set(pattern.findall(stripped))


@pytest.mark.parametrize("qml_file", sorted(QML_DIR.glob("*.qml")), ids=lambda p: p.name)
def test_every_viewmodel_member_used_in_qml_exists(qml_file: Path) -> None:
    for vm_name, member in sorted(references(qml_file.read_text(encoding="utf-8"))):
        assert member in exposed_names(BOUND_OBJECTS[vm_name]), (
            f"{qml_file.name} uses `{vm_name}.{member}`, which is not a Property "
            f"or Slot on {BOUND_OBJECTS[vm_name].__name__}. QML would fail at "
            f"runtime, not here."
        )


def test_the_refresh_gesture_reaches_every_source_on_the_screen() -> None:
    """Both screens draw on more than one provider; refresh must cover them all.

    This is a regression test. `refreshCurrent()` in Main.qml and both
    pull-to-refresh handlers used to call `refresh()`, which on the Dining
    screen fetches the public menu API and *not* the meal-plan balances — so
    the meals-left and dollar figures loaded once at sign-in and never moved
    again, no matter how hard you pulled.
    """
    for name in ("Main.qml", "ChapelView.qml", "DiningView.qml", "ChucksView.qml", "SummaryView.qml"):
        source = COMMENT_RE.sub("", (QML_DIR / name).read_text(encoding="utf-8"))
        for vm_name in VIEWMODELS:
            assert not re.search(rf"\b{vm_name}\.refresh\(\)", source), (
                f"{name} calls `{vm_name}.refresh()` as a user-facing refresh. "
                f"Use `{vm_name}.refreshAll()`, which also reloads the other "
                f"source on that screen."
            )


def test_refreshall_exists_on_every_viewmodel() -> None:
    for name, cls in VIEWMODELS.items():
        assert "refreshAll" in exposed_names(cls), name
