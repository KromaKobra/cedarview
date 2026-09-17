"""Interactive login, and how we know it worked.

.. rubric:: Why a WebView and not a token flow

``selfservice.cedarville.edu`` is a SAML 2.0 Service Provider federated to
Entra ID (tenant ``81c32413-015d-4ba8-a93b-e1c28e355738``). Verified live:
requesting ``/cedarinfo/chapelskip`` unauthenticated returns

    ``302 https://login.microsoftonline.com/81c32413-…/saml2?SAMLRequest=…&RelayState=%2Fcedarinfo%2Fchapelskip``

There is no OAuth client we could register against it, so MSAL and every
token-based flow are out. Interactive login in an embedded browser is the only
workable approach — and it has a real advantage: **the app never sees your
password.** You type it on Microsoft's own page, MFA included, and we only ever
observe which URL the browser ended up on.

.. rubric:: The flow

``RelayState`` carries the return path, so we do not need a separate "login
page" concept at all:

1. Point the surface at the target path (e.g. ``/cedarinfo/chapelskip``).
2. **If the session is alive**, it loads. The surface stays hidden and we go
   straight to fetching data.
3. **If it is not**, the 302 to Microsoft fires. We show the surface, you sign
   in, SAML posts you back, and ``RelayState`` lands you on the original path —
   at which point we hide the surface again and continue.

Success is therefore defined as one thing, checked one way: *the surface's URL
is on the Self-Service origin and does not look like a sign-in page.* No cookie
inspection, no timing assumptions, nothing that Entra can change under us.
"""

from __future__ import annotations

import logging

from PySide6.QtCore import Property, QObject, Signal, Slot

from ..core.session import SessionStore
from ..core.transport import BASE_URL, is_selfservice, looks_like_login, resolve

log = logging.getLogger(__name__)

#: Microsoft's federated sign-out endpoint. Navigating here ends the Entra
#: session; ``post_logout_redirect_uri`` brings the browser back somewhere
#: harmless so the user is not left staring at a Microsoft page.
LOGOUT_URL = (
    "https://login.microsoftonline.com/81c32413-015d-4ba8-a93b-e1c28e355738"
    "/oauth2/v2.0/logout?post_logout_redirect_uri=" + BASE_URL
)


class LoginController(QObject):
    """Owns the login surface's visibility and decides when we are signed in.

    Exposed to QML as the context property ``login``.
    """

    #: The session is now good; whoever was waiting may retry their request.
    loggedIn = Signal()

    #: The surface must become visible so the user can complete a sign-in.
    loginNeeded = Signal()

    #: Sign-out finished; the app should return to its signed-out state.
    signedOut = Signal()

    surfaceVisibleChanged = Signal()
    navigateRequested = Signal(str)
    statusChanged = Signal()
    sessionChanged = Signal()

    def __init__(
        self,
        store: SessionStore,
        backend,
        *,
        offline: bool = False,
        parent: QObject | None = None,
    ) -> None:
        super().__init__(parent)
        self._store = store
        self._state = store.load()
        self._backend = backend
        #: In ``--demo`` there is no browser and no session; the whole flow is
        #: short-circuited so the UI does not claim to be "connecting".
        self._offline = offline
        self._surface_visible = False
        self._status = ""
        self._return_path = ""
        self._signing_out = False

    # ------------------------------------------------------------------
    # QML-visible state
    # ------------------------------------------------------------------

    @Property(bool, notify=surfaceVisibleChanged)
    def surfaceVisible(self) -> bool:
        """Whether the WebView should be on screen.

        False during normal operation — the browser is an implementation detail
        of the transport and the user should never see it. True only while an
        interactive sign-in is in progress.
        """
        return self._surface_visible

    def _set_surface_visible(self, value: bool) -> None:
        if self._surface_visible != value:
            self._surface_visible = value
            self.surfaceVisibleChanged.emit()

    @Property(str, notify=statusChanged)
    def status(self) -> str:
        return self._status

    def _set_status(self, text: str) -> None:
        if self._status != text:
            self._status = text
            self.statusChanged.emit()
            log.info("login: %s", text)

    @Property(bool, notify=sessionChanged)
    def hasLoggedInBefore(self) -> bool:
        """True once a sign-in has completed on this device.

        Only drives first-run copy ("Sign in to get started" vs "Reconnecting").
        It is never treated as evidence that the session is still valid.
        """
        return self._state.has_logged_in

    # ------------------------------------------------------------------
    # Flow
    # ------------------------------------------------------------------

    @Slot(str)
    def begin(self, return_path: str) -> None:
        """Navigate to ``return_path``, signing in first if necessary.

        Safe to call on a live session: the page simply loads and nothing is
        shown to the user.
        """
        self._return_path = return_path or "/"
        self._signing_out = False

        if self._offline:
            self._set_status("Demo mode — showing saved fixtures")
            return

        self._set_status("Connecting to Cedarville…")
        self.navigateRequested.emit(resolve(self._return_path))

    @Slot(str)
    def onUrlChanged(self, url: str) -> None:
        """React to the surface navigating. Wired to the QML surface's URL.

        This is the whole state machine. Three cases, in order:

        * **Sign-out in progress** and we are back on Self-Service — finish up.
        * **On an identity provider** — show the surface; the user must act.
        * **On Self-Service and not a sign-in page** — we are in. Hide the
          surface and tell whoever was waiting.
        """
        if not url:
            return

        if self._signing_out:
            # Only the landing back on Self-Service ends the sign-out. Matching
            # on anything in the logout URL itself (it carries
            # `post_logout_redirect_uri`) would declare victory the instant we
            # navigated, before Entra had actually dropped the session.
            if is_selfservice(url):
                self._signing_out = False
                self._set_surface_visible(False)
                self._set_status("Signed out.")
                self.signedOut.emit()
            return

        if looks_like_login(url):
            if not self._surface_visible:
                self._set_status("Sign in with your Cedarville account")
                self._set_surface_visible(True)
                self.loginNeeded.emit()
            return

        if is_selfservice(url):
            was_visible = self._surface_visible
            self._set_surface_visible(False)
            self._set_status("")
            if was_visible or not self._state.has_logged_in:
                self._state = self._store.mark_login(self._state)
                self.sessionChanged.emit()
            self.loggedIn.emit()

    @Slot()
    def onSessionExpired(self) -> None:
        """A request came back from the identity provider. Re-run the flow.

        Called from :attr:`WebViewTransport.sessionExpired`. Identical to
        :meth:`begin` except for the message, because the remedy for an expired
        session is exactly the remedy for never having signed in.
        """
        self._state = self._store.mark_expiry(self._state)
        self.begin(self._return_path or "/")
        # After begin(), which sets its own first-connect message. If the
        # session is in fact still good this is replaced again the moment the
        # page loads, so the user never sees it for a non-event.
        self._set_status("Your session ended. Signing in again…")

    @Slot()
    def signOut(self) -> None:
        """End the session: clear cookies and forget our metadata.

        On desktop this also empties QtWebEngine's cookie store directly. On
        Android there is no cookie API, so the federated logout round trip is
        the mechanism — see :meth:`mycu.platform.android.AndroidBackend.clear_cookies`.
        """
        self._signing_out = True
        self._set_status("Signing out…")
        try:
            self._backend.clear_cookies()
        except Exception as exc:  # noqa: BLE001 - never let sign-out crash
            log.warning("backend cookie clear failed (continuing): %s", exc)

        self._store.clear()
        self._state = self._store.load()
        self.sessionChanged.emit()
        self._set_surface_visible(True)
        self.navigateRequested.emit(LOGOUT_URL)
