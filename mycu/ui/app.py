"""Application entry point: assembles everything and starts the event loop.

Startup order is load-bearing and is the one thing in this file worth reading
carefully:

1. ``backend.before_app()`` — QtWebEngine must initialise **before**
   ``QGuiApplication`` exists. Getting this wrong crashes rather than raising.
2. Create ``QGuiApplication``.
3. ``backend.after_app(app)`` — QtWebView must initialise **after** the
   application exists but **before** the QML engine loads. The mirror image of
   step 1, which is why the backend has two hooks instead of one.
4. Build the object graph, expose it to QML, load ``Main.qml``.

Run it:

    python -m mycu                      # real thing: log in, fetch live data
    python -m mycu --demo               # fixtures only; no network, no login
    python -m mycu --demo -v            # …with debug logging
"""

from __future__ import annotations

import argparse
import logging
import os
import sys
from pathlib import Path

from PySide6.QtCore import Property, QObject, QUrl, Slot
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine

from .. import __version__, platform as mycu_platform
from ..core.providers.chapel import CHAPEL_PATH
from ..core.session import SessionStore
from ..core.providers.chapel_schedule import CHAPEL_MEDIA_BASE
from ..core.transport import (
    DINING_BASE,
    FixtureTransport,
    HttpTransport,
    TransportRouter,
)
from .login import LoginController
from .settings import SettingsController
from .transport_webview import WebViewTransport
from .viewmodels.chapel import ChapelViewModel
from .viewmodels.dining import DiningViewModel
from .viewmodels.semester import SemesterViewModel

log = logging.getLogger("mycu")

QML_DIR = Path(__file__).parent / "qml"
FIXTURE_DIR = Path(__file__).resolve().parents[2] / "tests" / "fixtures"


class Bridge(QObject):
    """The few odds and ends QML needs that do not belong to a viewmodel."""

    def __init__(self, transport, start_path: str, platform_name: str, surface_qml: str) -> None:
        super().__init__()
        self._transport = transport
        self._start_path = start_path
        self._platform_name = platform_name
        self._surface_qml = surface_qml

    @Property(str, constant=True)
    def startPath(self) -> str:
        """Where the surface should navigate first.

        This is also the SAML ``RelayState`` value, so after a sign-in the
        browser lands back here on its own.
        """
        return self._start_path

    @Property(str, constant=True)
    def platformName(self) -> str:
        return self._platform_name

    @Property(str, constant=True)
    def surfaceQml(self) -> str:
        return self._surface_qml

    @Slot(QObject)
    def attachSurface(self, surface: QObject) -> None:
        """Hand the loaded QML surface to the transport.

        A no-op in demo mode, where the transport is a
        :class:`~mycu.core.transport.FixtureTransport` and there is no browser.
        """
        attach = getattr(self._transport, "attach_surface", None)
        if attach is None:
            log.info("demo mode: web surface ignored")
            return
        attach(surface)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="mycu", description="Your Cedarville records.")
    parser.add_argument(
        "--demo",
        action="store_true",
        help="run against tests/fixtures/ instead of the network — no login, "
             "no browser, no Cedarville. Use this to work on the UI.",
    )
    parser.add_argument(
        "--fixtures",
        type=Path,
        default=FIXTURE_DIR,
        help=f"fixture directory for --demo (default: {FIXTURE_DIR})",
    )
    parser.add_argument(
        "--state-dir",
        type=Path,
        default=None,
        help="override where session metadata and the cookie jar live",
    )
    parser.add_argument("-v", "--verbose", action="store_true", help="debug logging")
    parser.add_argument("--version", action="version", version=f"CedarView {__version__}")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(levelname)s %(name)s: %(message)s",
        # stdout, not stderr: on Android `adb logcat` picks up both, but stdout
        # is what python-for-android tags with the app's name.
        stream=sys.stdout,
    )

    backend = mycu_platform.current_backend()
    log.info("CedarView %s — %s", __version__, mycu_platform.describe())

    # --- 1. Web engine init that must precede the application object ---------
    if not args.demo:
        backend.before_app()

    # --- 2. The application -------------------------------------------------
    app = QGuiApplication(sys.argv[:1])
    # The app is called CedarView. `mycu` survives as the Python package, the
    # p4a dist name and the Android applicationId (org.mycu.mycu) — renaming
    # those would make this a different app to Android and orphan everyone's
    # session, which is not worth a tidier import path.
    #
    # These two decide where QSettings writes (~/.config/Kroma/CedarView.conf
    # on Linux, app-private storage on Android), so
    # changing them resets the theme preference once, on each device. That is
    # the whole cost; the login is not stored here — see mycu.core.session,
    # which keys off its own APP_DIR_NAME and is untouched.
    app.setApplicationName("CedarView")
    app.setOrganizationName("Kroma")
    app.setApplicationVersion(__version__)

    # --- 3. Web engine init that must follow it -----------------------------
    if not args.demo:
        backend.after_app(app)

    # --- 4. Object graph ----------------------------------------------------
    store = SessionStore(args.state_dir)
    store.ensure_dirs()

    # The app talks to two services with different auth, so requests are routed
    # by origin (see TransportRouter):
    #
    #   selfservice.cedarville.edu   SAML/Entra session   -> the WebView
    #   diningdata.cedarville.edu    none at all          -> plain HTTP
    #
    # Routing is a correctness requirement, not a shortcut: an in-page fetch()
    # is bound by the same-origin policy, so a WebView parked on Self-Service
    # could not reach the dining API even if we wanted it to.
    if args.demo:
        session_transport = FixtureTransport(args.fixtures)
        transport = session_transport
        surface_qml = "WebSurfaceStub.qml"
        log.info("demo mode: serving fixtures from %s", args.fixtures)
    else:
        session_transport = WebViewTransport()
        # Both of these are public services with no session of their own, so
        # they go straight out over HTTP rather than through the browser.
        transport = (
            TransportRouter(session_transport)
            .route(DINING_BASE, HttpTransport())
            .route(CHAPEL_MEDIA_BASE, HttpTransport())
        )
        surface_qml = backend.surface_qml
        backend.configure_profile(store.profile_dir())

    login = LoginController(store, backend, offline=args.demo)
    chapel = ChapelViewModel(transport, store)
    dining = DiningViewModel(transport)
    # No transport: the term's dates are in mycu.core.calendar, because no
    # Cedarville service publishes them. See that module for the apology.
    semester = SemesterViewModel()
    # QSettings with no arguments, so it must be built after setApplicationName
    # and setOrganizationName above — otherwise it writes to a file named after
    # the executable.
    settings = SettingsController()
    bridge = Bridge(session_transport, CHAPEL_PATH, backend.name, surface_qml)

    # The expiry loop, in two connections:
    #   a failed fetch  -> reopen the sign-in surface
    #   a completed login -> retry the fetch
    chapel.sessionExpired.connect(login.onSessionExpired)
    login.loggedIn.connect(chapel.refresh)
    login.loggedIn.connect(dining.refreshPlan)
    login.signedOut.connect(lambda: log.info("signed out"))

    if not args.demo:
        # Neither of these needs a session, so they do not wait for the login
        # flow — the menu and the next speaker are on screen while you sign in.
        dining.refresh()
        chapel.refreshSchedule()

    if not args.demo:
        # The WebView transport also detects expiry directly, before the
        # exception has propagated back through the worker thread — connecting
        # both means the login surface appears as soon as we know, not a beat
        # later. Note this is `session_transport`, not the router: only the
        # session-bearing transport can have an expired session.
        session_transport.sessionExpired.connect(login.onSessionExpired)

    # --- 5. QML -------------------------------------------------------------
    engine = QQmlApplicationEngine()
    ctx = engine.rootContext()
    ctx.setContextProperty("bridge", bridge)
    ctx.setContextProperty("login", login)
    ctx.setContextProperty("chapel", chapel)
    ctx.setContextProperty("dining", dining)
    ctx.setContextProperty("semester", semester)
    # Read by every Theme.qml instance, which is how eight separate copies of
    # the palette agree on which one is showing.
    ctx.setContextProperty("settings", settings)
    ctx.setContextProperty("platformSurface", surface_qml)
    # Desktop only: the persistent profile WebSurfaceDesktop.qml binds to.
    ctx.setContextProperty("webProfile", backend.qml_profile())

    engine.addImportPath(str(QML_DIR))
    engine.load(QUrl.fromLocalFile(str(QML_DIR / "Main.qml")))

    if not engine.rootObjects():
        log.error(
            "QML failed to load. On desktop this is almost always QtWebEngine "
            "missing from QML2_IMPORT_PATH — are you in the nix devShell?"
        )
        return 1

    if args.demo:
        # Nothing will trigger the first load, since there is no login flow.
        chapel.refreshAll()
        dining.refreshAll()

    try:
        return app.exec()
    finally:
        # Tear down in dependency order rather than leaving it to Python, which
        # frees main()'s locals in no particular order: QtWebEngine segfaults
        # if the web profile goes before the views using it.
        del engine
        backend.shutdown()


def run() -> None:
    """Console-script wrapper."""
    raise SystemExit(main())


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
