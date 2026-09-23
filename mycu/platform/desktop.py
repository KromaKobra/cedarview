"""Desktop backend: QtWebEngine (Chromium, in-process).

Confirmed present in the pinned nixpkgs (``nixos-26.05``): PySide6 6.11.0,
Qt 6.11.1, qtwebengine 6.11.1 with its QML module installed. ``flake.nix`` puts
``qt6.qtwebengine`` on ``QML2_IMPORT_PATH`` and exports
``QTWEBENGINEPROCESS_PATH``; without those two the ``QtWebEngine`` QML import
fails at load time with an unhelpful message.

Unlike Android, this backend *can* see cookies, including ``HttpOnly`` ones,
through ``QWebEngineCookieStore``. We deliberately do not use that: the
transport makes its request from inside the page on both platforms so there is
exactly one code path to debug. The cookie store is used only to clear cookies
on sign-out.
"""

from __future__ import annotations

import logging
from pathlib import Path

log = logging.getLogger(__name__)


class DesktopBackend:
    """QtWebEngine-backed web surface."""

    name = "desktop"
    surface_qml = "WebSurfaceDesktop.qml"

    def __init__(self) -> None:
        self._profile = None

    def before_app(self) -> None:
        """Initialise Chromium before the application object exists.

        QtWebEngine spins up its own process infrastructure and must do so
        before ``QGuiApplication``. Calling this late produces a hard crash
        rather than a Python exception, so it is done first thing in
        :func:`mycu.ui.app.main`.
        """
        from PySide6.QtWebEngineQuick import QtWebEngineQuick

        QtWebEngineQuick.initialize()
        log.debug("QtWebEngineQuick initialised")

    def after_app(self, app: object) -> None:
        """Nothing to do — QtWebEngine is already up."""

    def configure_profile(self, storage_dir: Path) -> None:
        """Persist cookies under ``storage_dir`` so the login survives restarts.

        This has to be a profile of our own. In Qt 6 the default profile is
        off-the-record, and an off-the-record profile silently ignores
        ``setPersistentStoragePath`` and forces ``NoPersistentCookies`` — so
        configuring the default one looks right and persists nothing. Every
        launch would then start at the Microsoft sign-in page and trigger a
        fresh MFA prompt.

        The profile is handed to QML as ``webProfile`` (see :meth:`qml_profile`)
        and bound by ``WebSurfaceDesktop.qml``. Must run after
        ``QGuiApplication`` exists and before the QML engine loads.
        """
        from PySide6.QtWebEngineQuick import QQuickWebEngineProfile

        storage_dir.mkdir(parents=True, exist_ok=True, mode=0o700)

        # Order matters: setting the storage name resets the paths to their
        # defaults, and it does not by itself clear the off-the-record flag.
        profile = QQuickWebEngineProfile()
        profile.setStorageName("mycu")
        profile.setOffTheRecord(False)
        profile.setPersistentStoragePath(str(storage_dir))
        profile.setCachePath(str(storage_dir / "cache"))
        profile.setPersistentCookiesPolicy(
            QQuickWebEngineProfile.PersistentCookiesPolicy.ForcePersistentCookies
        )
        self._profile = profile
        log.info("web profile persisted at %s", storage_dir)

    def qml_profile(self) -> object | None:
        return self._profile

    def shutdown(self) -> None:
        """Drop the profile. Only safe after every view using it is destroyed."""
        self._profile = None

    def clear_cookies(self) -> None:
        if self._profile is None:
            return
        self._profile.cookieStore().deleteAllCookies()
        log.info("desktop cookies cleared")

    def user_agent_note(self) -> str:
        return ""
