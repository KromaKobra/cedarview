"""Session state: where it lives on disk, and what little of it we own.

An important thing to be clear about: **this module does not store cookies.**
The session cookie is `HttpOnly`, and on Android it is unreachable from Qt (the
`cookieAdded` signal only fires for cookies *we* set, which is exactly why the
transport lets the WebView make the request instead of harvesting the cookie).
The cookie jar is owned by the WebView:

* **Desktop** — QtWebEngine's persistent profile, stored under
  :meth:`SessionStore.profile_dir`.
* **Android** — the system WebView's own cookie store, in app-private storage.
  We neither see it nor manage it.

What this module *does* own is the small amount of metadata that makes the UI
behave sensibly across launches: have we ever logged in, when did a request last
succeed, and which term were we last looking at. It is written 0600 under
``$XDG_DATA_HOME`` (or ``filesDir`` on Android) and never goes near git.

The app never sees or stores your password. Login happens on Microsoft's own
page, inside the WebView.
"""

from __future__ import annotations

import json
import os
import time
from dataclasses import asdict, dataclass
from pathlib import Path

APP_DIR_NAME = "mycu"

#: Bumped whenever the on-disk shape of ``session.json`` changes. A file with a
#: different value is discarded rather than migrated — it holds nothing
#: expensive to regenerate.
SCHEMA_VERSION = 1


@dataclass(slots=True)
class SessionState:
    """Non-secret metadata about the current session.

    Nothing here is a credential. If this file leaked it would reveal that you
    use the app and roughly when — which is why it is still written 0600, but
    also why losing it costs you nothing more than one extra login.
    """

    #: Unix time of the last completed interactive login, 0 if never.
    last_login: float = 0.0
    #: Unix time of the last request that came back with real data.
    last_success: float = 0.0
    #: Unix time we last detected expiry, for "your session ended" messaging.
    last_expiry: float = 0.0
    #: Whatever the chapel view was last showing, so a cold start looks right.
    last_term: str = ""
    #: Copy of :data:`SCHEMA_VERSION` at write time; see :meth:`SessionStore.load`.
    schema: int = SCHEMA_VERSION

    @property
    def has_logged_in(self) -> bool:
        """Whether we have ever completed a login on this device.

        Drives first-run behaviour only. It is *not* a claim that the session is
        still valid — that is never predicted, only discovered by making a
        request. See :func:`mycu.core.transport.looks_like_login`.
        """
        return self.last_login > 0


def default_state_dir() -> Path:
    """Where session state belongs on this platform.

    Android is detected via ``ANDROID_ARGUMENT``, which python-for-android sets
    in every app process; ``MYCU_STATE_DIR`` overrides both for tests and for
    running two profiles side by side.
    """
    override = os.environ.get("MYCU_STATE_DIR")
    if override:
        return Path(override)

    android_private = os.environ.get("ANDROID_PRIVATE") or os.environ.get("ANDROID_ARGUMENT")
    if android_private:
        # ANDROID_ARGUMENT points at the app's private data dir; ANDROID_PRIVATE
        # is the same thing on newer python-for-android. Either is app-private
        # and unreadable by other apps, which is what we want.
        return Path(android_private) / APP_DIR_NAME

    xdg = os.environ.get("XDG_DATA_HOME")
    base = Path(xdg) if xdg else Path.home() / ".local" / "share"
    return base / APP_DIR_NAME


class SessionStore:
    """Loads, saves and clears :class:`SessionState`.

    Deliberately forgiving on read: a corrupt or older-schema file is treated as
    "no session", because the cost of being wrong is one login, and the cost of
    crashing on startup is an app that cannot recover without a terminal.
    """

    FILENAME = "session.json"

    def __init__(self, state_dir: Path | str | None = None) -> None:
        self.state_dir = Path(state_dir) if state_dir else default_state_dir()

    @property
    def path(self) -> Path:
        return self.state_dir / self.FILENAME

    def profile_dir(self) -> Path:
        """Directory for the WebView's persistent cookie jar (desktop only).

        Handed to ``QWebEngineProfile.setPersistentStoragePath``. Android
        ignores this entirely — the system WebView manages its own store.
        """
        return self.state_dir / "webprofile"

    def ensure_dirs(self) -> None:
        """Create the state directory 0700 if it is not already there."""
        self.state_dir.mkdir(parents=True, exist_ok=True, mode=0o700)

    def load(self) -> SessionState:
        try:
            raw = json.loads(self.path.read_text(encoding="utf-8"))
        except (FileNotFoundError, json.JSONDecodeError, OSError):
            return SessionState()

        if not isinstance(raw, dict) or raw.get("schema") != SCHEMA_VERSION:
            return SessionState()

        known = set(SessionState.__slots__)
        return SessionState(**{k: v for k, v in raw.items() if k in known})

    def save(self, state: SessionState) -> None:
        """Write state atomically and 0600.

        Atomic because a half-written file on a crash would otherwise be read
        back as "no session" — recoverable, but an avoidable surprise.
        """
        self.ensure_dirs()
        tmp = self.path.with_suffix(".json.tmp")
        tmp.write_text(json.dumps(asdict(state), indent=2), encoding="utf-8")
        os.chmod(tmp, 0o600)
        tmp.replace(self.path)

    def mark_login(self, state: SessionState) -> SessionState:
        state.last_login = time.time()
        self.save(state)
        return state

    def mark_success(self, state: SessionState) -> SessionState:
        state.last_success = time.time()
        self.save(state)
        return state

    def mark_expiry(self, state: SessionState) -> SessionState:
        state.last_expiry = time.time()
        self.save(state)
        return state

    def clear(self) -> None:
        """Forget the session metadata.

        Does **not** clear cookies — that is the WebView's job, and the UI does
        it through :meth:`mycu.ui.login.LoginController.clear_cookies` so both
        halves are dropped together.
        """
        self.path.unlink(missing_ok=True)
