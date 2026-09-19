"""UI preferences, and the single source of truth for which theme is on.

Backed by ``QSettings``, **not** by :class:`~mycu.core.session.SessionStore`.
The session store holds the cookie jar and is thrown away when you sign out; a
theme choice surviving a sign-out is the behaviour anyone would expect, and
coupling the two would mean the app forgot your palette every time the SAML
session expired. ``QSettings`` also already knows where to write on each
platform — ``~/.config/Kroma/CedarView.conf`` on Linux, app-private storage on
Android — which is one less path to resolve by hand.

The default constructor works because ``app.py`` sets the organisation and
application names on ``QGuiApplication`` before this is built. Constructing it
earlier would silently write to a file named after the executable.

**Why QML reads this rather than a property on Theme:** ``Theme.qml`` is a plain
``QtObject`` instantiated once per file, not a singleton (its own header comment
explains why). Eight independent instances need one shared answer, and a context
property is the cheapest thing that is genuinely shared — every ``Theme`` binds
``light`` to ``settings.lightMode`` and they all change together.
"""

from __future__ import annotations

import logging

from PySide6.QtCore import Property, QObject, QSettings, Signal, Slot

log = logging.getLogger(__name__)

#: One key, one group. The group exists so the next preference has an obvious
#: home and does not end up at the top level next to Qt's own bookkeeping.
LIGHT_MODE_KEY = "ui/lightMode"


class SettingsController(QObject):
    """Preferences that outlive the session. Exposed to QML as ``settings``."""

    changed = Signal()

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._settings = QSettings()
        # QSettings has no typed read on every backend — the INI backend hands
        # back the string "true", which is truthy either way it is spelled. Ask
        # for a bool explicitly and let Qt do the conversion.
        self._light = bool(self._settings.value(LIGHT_MODE_KEY, False, type=bool))
        log.debug("settings: lightMode=%s from %s", self._light, self._settings.fileName())

    @Property(bool, notify=changed)
    def lightMode(self) -> bool:
        return self._light

    @lightMode.setter  # type: ignore[no-redef]
    def lightMode(self, value: bool) -> None:
        self.setLightMode(value)

    @Slot(bool)
    def setLightMode(self, value: bool) -> None:
        """Set the theme and write it through immediately.

        ``sync()`` rather than waiting for the destructor: on Android the
        process is killed rather than exited, and a preference that only lands
        on a clean shutdown is a preference that mostly does not land.
        """
        value = bool(value)
        if value == self._light:
            return

        self._light = value
        self._settings.setValue(LIGHT_MODE_KEY, value)
        self._settings.sync()
        self.changed.emit()

    @Slot()
    def toggleLightMode(self) -> None:
        self.setLightMode(not self._light)
