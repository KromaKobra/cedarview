"""Android backend: QtWebView (the system WebView).

This is the only file that Android needs and desktop never loads.

.. rubric:: Why the transport works the way it does

``QtWebEngine`` does not exist on Android. Its substitute, ``QtWebView``, wraps
the OS WebView, and Qt's documentation is explicit about the consequence:

    *"When Qt WebEngine module is used as backend, cookieAdded signal will be
    emitted for any cookie added to the underlying QWebEngineCookieStore,
    including those added by websites.* **In other cases cookieAdded signal is
    only emitted for cookies explicitly added with setCookie().**\\ *"*

So on Android we cannot read the ``HttpOnly`` ASP.NET session cookie, and
therefore cannot hand it to an HTTP client. We do not need to. The WebView
already has the cookie and attaches it automatically — so we let *it* make the
request, from inside the logged-in page, and pass the body back through
``runJavaScript``. Both methods that requires (``url``, ``runJavaScript``) are
present on both backends. See :mod:`mycu.ui.transport_webview`.

.. rubric:: Initialisation order

``QtWebView.initialize()`` must be called **after** ``QGuiApplication`` exists
but **before** the QML engine loads anything — the mirror image of
QtWebEngine's requirement. Hence :meth:`after_app`.

.. rubric:: Not yet run on a device

Everything here follows from documented Qt behaviour and from the transport
technique that was verified working. It has **not** been executed on hardware —
see ``docs/next-steps.md`` for exactly what to check first, and ``# VERIFY:``
comments below for the individual claims that on-device testing will confirm or
refute.
"""

from __future__ import annotations

import logging
from pathlib import Path

log = logging.getLogger(__name__)


class AndroidBackend:
    """QtWebView-backed web surface."""

    name = "android"
    surface_qml = "WebSurfaceAndroid.qml"

    def before_app(self) -> None:
        """Nothing — QtWebView must not be touched before the app exists."""

    def after_app(self, app: object) -> None:
        """Initialise QtWebView.

        Failure here is fatal and worth a loud message: without QtWebView there
        is no way to log in, so there is nothing to degrade to.
        """
        try:
            from PySide6.QtWebView import QtWebView
        except ImportError as exc:  # pragma: no cover - device-only path
            raise RuntimeError(
                "PySide6.QtWebView is missing from this Android build. "
                "pyside6-android-deploy must be told to include the WebView "
                "module — see docs/android.md."
            ) from exc

        QtWebView.initialize()
        log.info("QtWebView initialised")

    def configure_profile(self, storage_dir: Path) -> None:
        """No-op: the system WebView owns its cookie jar.

        Android already keeps it in app-private storage, which is where we
        wanted it. Nothing for us to configure, and nothing for us to leak.
        """
        log.debug("android: cookie persistence is handled by the system WebView")

    def qml_profile(self) -> object | None:
        """None: QtWebView has no profile object to hand to QML."""
        return None

    def shutdown(self) -> None:
        """Nothing to release."""

    def clear_cookies(self) -> None:
        """Clear cookies by asking the WebView layer, not the cookie store.

        QtWebView exposes no cookie API at all, so "sign out" on Android is
        implemented in QML instead: :meth:`mycu.ui.login.LoginController.sign_out`
        navigates the surface to the Microsoft logout endpoint and then clears
        our own session metadata.

        # VERIFY: on-device, confirm that a logout round trip really does drop
        # the Self-Service cookie, rather than only the Entra one. If it does
        # not, the fallback is `android.webkit.CookieManager.removeAllCookies`
        # via JNI — which needs `QJniObject` to exist in the Android PySide6
        # build (unproven; see the M4 note in the plan).
        """
        log.info("android: cookie clearing is delegated to LoginController.sign_out")

    def user_agent_note(self) -> str:
        return ""
