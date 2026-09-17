"""Shared pytest setup.

Two guarantees this file enforces for the whole suite:

1. **No test can reach the network.** Every socket-creating entry point is
   monkeypatched to raise. A test that quietly hits ``selfservice.cedarville.edu``
   would pass on your laptop, fail in any other context, and — much worse —
   would mean the suite's results depend on a live session. Opt a test out
   explicitly with ``@pytest.mark.allow_network``; only ``scripts/check-live``
   is expected to want it, and that is not a test.

2. **Qt never needs a display.** ``QT_QPA_PLATFORM=offscreen`` is set before
   PySide6 can be imported, so the handful of tests that touch Qt run under CI,
   over SSH, and in a nix build sandbox.
"""

from __future__ import annotations

import os
import socket
from pathlib import Path

import pytest

# Must happen before anything imports PySide6.
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
# Keep tests away from the real session directory in ~/.local/share.
os.environ.setdefault("MYCU_STATE_DIR", "")

FIXTURES = Path(__file__).parent / "fixtures"
SAMPLES = FIXTURES / "samples"


def pytest_configure(config: pytest.Config) -> None:
    config.addinivalue_line(
        "markers", "allow_network: let this test open real sockets (almost never right)"
    )


class _NetworkBlocked(RuntimeError):
    pass


@pytest.fixture(autouse=True)
def no_network(request: pytest.FixtureRequest, monkeypatch: pytest.MonkeyPatch) -> None:
    """Fail loudly on any attempt to open a socket."""
    if request.node.get_closest_marker("allow_network"):
        return

    def blocked(*args, **kwargs):
        raise _NetworkBlocked(
            "this test tried to use the network. Parsing tests must run against "
            "tests/fixtures/; if you genuinely need a live request, that belongs "
            "in scripts/check-live, not in the suite."
        )

    monkeypatch.setattr(socket, "socket", blocked)
    monkeypatch.setattr(socket, "create_connection", blocked)
    monkeypatch.setattr(socket, "getaddrinfo", blocked)


@pytest.fixture(autouse=True)
def isolated_state(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    """Point session storage at a temp dir so tests never touch real state."""
    state = tmp_path / "state"
    monkeypatch.setenv("MYCU_STATE_DIR", str(state))
    return state


@pytest.fixture
def fixtures_dir() -> Path:
    return FIXTURES


@pytest.fixture
def samples_dir() -> Path:
    return SAMPLES


@pytest.fixture
def chapel_html(fixtures_dir: Path) -> str:
    return (fixtures_dir / "cedarinfo_chapelskip.html").read_text(encoding="utf-8")


@pytest.fixture
def login_html(samples_dir: Path) -> str:
    return (samples_dir / "entra_login_page.html").read_text(encoding="utf-8")
