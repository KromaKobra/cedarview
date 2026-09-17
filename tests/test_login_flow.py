"""The login state machine, driven by URLs alone.

This is the whole of the auth design, and it is testable without a browser
precisely because its only input is "which URL is the surface on". No cookie
inspection, no timers, nothing Entra can change under us.
"""

from __future__ import annotations

from pathlib import Path

import pytest

pytest.importorskip("PySide6.QtCore", reason="PySide6 not available")

from mycu.core.session import SessionStore  # noqa: E402
from mycu.core.transport import BASE_URL  # noqa: E402
from mycu.ui.login import LOGOUT_URL, LoginController  # noqa: E402

CHAPEL = f"{BASE_URL}/cedarinfo/chapelskip"
IDP = (
    "https://login.microsoftonline.com/81c32413-015d-4ba8-a93b-e1c28e355738"
    "/saml2?SAMLRequest=abc&RelayState=%2Fcedarinfo%2Fchapelskip"
)


class FakeBackend:
    name = "fake"
    surface_qml = "WebSurfaceStub.qml"

    def __init__(self) -> None:
        self.cleared = 0

    def clear_cookies(self) -> None:
        self.cleared += 1


class Recorder:
    """Collects every signal the controller emits, in order."""

    def __init__(self, controller: LoginController) -> None:
        self.navigations: list[str] = []
        self.events: list[str] = []
        controller.navigateRequested.connect(self.navigations.append)
        controller.loggedIn.connect(lambda: self.events.append("loggedIn"))
        controller.loginNeeded.connect(lambda: self.events.append("loginNeeded"))
        controller.signedOut.connect(lambda: self.events.append("signedOut"))


@pytest.fixture
def setup(tmp_path: Path):
    backend = FakeBackend()
    controller = LoginController(SessionStore(tmp_path), backend)
    return controller, backend, Recorder(controller)


# ---------------------------------------------------------------------------

def test_begin_navigates_to_the_target_not_to_a_login_page(setup) -> None:
    """RelayState carries the return path, so there is no separate login step."""
    controller, _, rec = setup
    controller.begin("/cedarinfo/chapelskip")

    assert rec.navigations == [CHAPEL]
    assert controller.surfaceVisible is False


def test_a_live_session_never_shows_the_browser(setup) -> None:
    controller, _, rec = setup
    controller.begin("/cedarinfo/chapelskip")
    controller.onUrlChanged(CHAPEL)

    assert controller.surfaceVisible is False
    assert "loginNeeded" not in rec.events
    assert rec.events == ["loggedIn"]


def test_landing_on_the_idp_raises_the_sign_in_surface(setup) -> None:
    controller, _, rec = setup
    controller.begin("/cedarinfo/chapelskip")
    controller.onUrlChanged(IDP)

    assert controller.surfaceVisible is True
    assert rec.events == ["loginNeeded"]
    assert "Sign in" in controller.status


def test_coming_back_to_selfservice_completes_the_login(setup) -> None:
    controller, _, rec = setup
    controller.begin("/cedarinfo/chapelskip")
    controller.onUrlChanged(IDP)
    controller.onUrlChanged(CHAPEL)

    assert controller.surfaceVisible is False
    assert rec.events == ["loginNeeded", "loggedIn"]
    assert controller.hasLoggedInBefore is True


def test_intermediate_idp_hops_do_not_re_signal(setup) -> None:
    """Entra bounces through several URLs; that is one sign-in, not five."""
    controller, _, rec = setup
    controller.begin("/cedarinfo/chapelskip")
    for url in (IDP, IDP + "&sso_reload=true", "https://login.microsoftonline.com/common/DeviceAuth"):
        controller.onUrlChanged(url)

    assert rec.events == ["loginNeeded"]


def test_an_empty_url_is_ignored(setup) -> None:
    controller, _, rec = setup
    controller.onUrlChanged("")
    assert rec.events == []


def test_expiry_restarts_the_same_flow(setup) -> None:
    """The remedy for an expired session is the remedy for never having one."""
    controller, _, rec = setup
    controller.begin("/cedarinfo/chapelskip")
    controller.onUrlChanged(CHAPEL)
    rec.navigations.clear()

    controller.onSessionExpired()

    assert rec.navigations == [CHAPEL]
    assert "session ended" in controller.status


def test_the_login_survives_a_restart(tmp_path: Path) -> None:
    first = LoginController(SessionStore(tmp_path), FakeBackend())
    first.begin("/cedarinfo/chapelskip")
    first.onUrlChanged(IDP)
    first.onUrlChanged(CHAPEL)

    second = LoginController(SessionStore(tmp_path), FakeBackend())
    assert second.hasLoggedInBefore is True


# ---------------------------------------------------------------------------
# Sign out
# ---------------------------------------------------------------------------

def test_sign_out_clears_cookies_and_goes_to_the_federated_logout(setup) -> None:
    controller, backend, rec = setup
    controller.begin("/cedarinfo/chapelskip")
    controller.onUrlChanged(CHAPEL)
    rec.navigations.clear()

    controller.signOut()

    assert backend.cleared == 1
    assert rec.navigations == [LOGOUT_URL]
    assert controller.surfaceVisible is True
    assert controller.hasLoggedInBefore is False


def test_sign_out_only_completes_once_we_are_back_on_selfservice(setup) -> None:
    """Not the moment we navigate — the logout URL contains 'post_logout'."""
    controller, _, rec = setup
    controller.signOut()

    controller.onUrlChanged(LOGOUT_URL)
    assert "signedOut" not in rec.events
    assert controller.surfaceVisible is True

    controller.onUrlChanged(BASE_URL + "/")
    assert "signedOut" in rec.events
    assert controller.surfaceVisible is False


def test_sign_out_survives_a_backend_that_cannot_clear_cookies(tmp_path: Path) -> None:
    """Android has no cookie API; sign-out must still work there."""

    class Stubborn(FakeBackend):
        def clear_cookies(self) -> None:
            raise RuntimeError("no cookie API on this platform")

    controller = LoginController(SessionStore(tmp_path), Stubborn())
    rec = Recorder(controller)

    controller.signOut()           # must not raise
    assert rec.navigations == [LOGOUT_URL]


# ---------------------------------------------------------------------------
# Demo mode
# ---------------------------------------------------------------------------

def test_demo_mode_never_touches_the_network(tmp_path: Path) -> None:
    controller = LoginController(SessionStore(tmp_path), FakeBackend(), offline=True)
    rec = Recorder(controller)

    controller.begin("/cedarinfo/chapelskip")

    assert rec.navigations == []
    assert controller.surfaceVisible is False
    assert "Demo" in controller.status
