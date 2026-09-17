"""The interface a web backend must satisfy."""

from __future__ import annotations

from abc import ABC, abstractmethod
from pathlib import Path


class WebBackend(ABC):
    """Platform-specific setup for the embedded browser.

    Three responsibilities, and nothing else:

    1. Initialise the web engine at the right point in application startup
       (:meth:`before_app` / :meth:`after_app`).
    2. Say which QML file provides the web surface (:attr:`surface_qml`) —
       the two files expose an identical interface, so no other QML differs.
    3. Configure persistence, where the platform lets us.
    """

    #: ``"desktop"`` or ``"android"``.
    name: str = ""

    #: Filename under ``mycu/ui/qml/`` implementing the web surface.
    surface_qml: str = ""

    def before_app(self) -> None:
        """Run before ``QGuiApplication`` is constructed.

        QtWebEngine requires this; QtWebView must *not* be initialised here.
        """

    def after_app(self, app: object) -> None:
        """Run after ``QGuiApplication`` exists, before the QML engine loads.

        QtWebView requires this; QtWebEngine ignores it.
        """

    def configure_profile(self, storage_dir: Path) -> None:
        """Point the browser's persistent storage at ``storage_dir``.

        Desktop honours this so the session survives a relaunch. Android cannot:
        the system WebView owns its cookie jar and keeps it in app-private
        storage already, which is the behaviour we wanted anyway.
        """

    @abstractmethod
    def clear_cookies(self) -> None:
        """Drop all cookies, forcing a fresh login.

        Used by "Sign out" and by the session-expiry test in
        ``docs/testing.md``.
        """

    def user_agent_note(self) -> str:
        """Extra User-Agent text, if the backend lets us set one."""
        return ""
