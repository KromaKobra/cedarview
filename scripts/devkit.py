"""What the desktop dev scripts share, now that the app itself is C++.

``scripts/discover``, ``scripts/check-live`` and ``scripts/smoke-transport``
are Python tools that drive a real QtWebEngine window through PySide6. They
used to import the app's own Python package for the pieces below; the app no
longer has one (it is src/ and qml/), so this module carries the minimum they
need, self-contained:

* the Self-Service origin and :func:`looks_like_login`, the expiry test;
* the error types the scripts catch;
* :class:`DesktopWeb` — QtWebEngine start-up and the persistent profile, the
  same way the app's src/platform/desktop.cpp does it;
* :class:`WebViewTransport` — the in-page ``fetch()`` transport, with the same
  scripts, polling and timeouts as src/ui/webviewtransport.cpp;
* :class:`Login` — the URL-driven sign-in state machine, cut down to what a
  script needs from src/ui/login.cpp;
* :func:`run_in_background` and :func:`state_dir`.

Desktop only, and never part of the app: nothing here reaches the APK. Keep the
behaviour in step with the C++ it mirrors — if the app's expiry rules or fetch
script change, change them here too.
"""

from __future__ import annotations

import json
import logging
import os
import re
import threading
import time
import traceback
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable
from urllib.parse import urlsplit

log = logging.getLogger("devkit")

ROOT = Path(__file__).resolve().parents[1]
QML_DIR = ROOT / "qml"
FIXTURES = ROOT / "tests" / "fixtures"

# ---------------------------------------------------------------------------
# Origins and expiry (src/core/transport.h)
# ---------------------------------------------------------------------------

BASE_URL = "https://selfservice.cedarville.edu"

IDP_HOSTS = (
    "login.microsoftonline.com",
    "login.microsoft.com",
    "login.windows.net",
    "sts.cedarville.edu",
    "adfs.cedarville.edu",
)

LOGIN_BODY_MARKERS = (
    "SAMLRequest",
    "urn:oasis:names:tc:SAML",
    '$Config={"fShowPersistentCookiesWarning"',
    "ConvergedSignIn",
    "Sign in to your account",
)


class MycuError(Exception):
    """Base class for every error these tools raise deliberately."""


class SessionExpired(MycuError):
    """The request landed on an identity provider."""


class TransportError(MycuError):
    """The request could not be completed for a reason other than auth."""


class ParseError(MycuError):
    """The response arrived but did not have the expected shape."""


def resolve(path: str) -> str:
    if path.startswith(("http://", "https://")):
        return path
    return BASE_URL + "/" + path.lstrip("/")


def is_selfservice(url: str) -> bool:
    return url.startswith(BASE_URL)


def looks_like_login(url: str, body: str = "") -> bool:
    """Whether this is a sign-in page rather than data. Matches on the HOST."""
    host = (urlsplit(url or "").hostname or "").lower()
    if host and any(host == idp or host.endswith("." + idp) for idp in IDP_HOSTS):
        return True
    if body:
        head = body[:8192]
        if any(marker in head for marker in LOGIN_BODY_MARKERS):
            return True
        if re.search(r'<form[^>]+action="https://login\.microsoft', head, re.I):
            return True
    return False


@dataclass(slots=True)
class Response:
    status: int
    url: str
    body: str
    headers: dict[str, str] = field(default_factory=dict)

    @property
    def ok(self) -> bool:
        return 200 <= self.status < 300

    def json(self) -> object:
        try:
            return json.loads(self.body)
        except (json.JSONDecodeError, TypeError) as exc:
            preview = (self.body or "")[:200].replace("\n", " ")
            raise ParseError(f"expected JSON from {self.url!r} but got {preview!r}") from exc


# ---------------------------------------------------------------------------
# Chapel (src/core/providers/chapel.cpp): what check-live compares
# ---------------------------------------------------------------------------

CHAPEL_PATH = "/cedarinfo/chapelskip"
SUMMARY_PATH = "/CedarInfo/ChapelSkip/GetStudentSummaryJson"
LEDGER_PATH = "/CedarInfo/ChapelSkip/GetStudentLedgerJson"

#: How the dashboard hands itself the student ID.
STUDENT_ID_RE = re.compile(r"""\bstudentId\s*=\s*['"](\d+)['"]""")

#: Every summary key the app reads, and the JSON type it expects of it.
SUMMARY_FIELDS = {
    "SkipsUsed": "number",
    "SkipsTotal": "number",
    "SkipsRemaining": "number",
    "Term": "string",
    "TermName": "string",
    "StudentName": "string",
    "StudentId": "string",
    "AllowanceBreakdown": "list",
    "RequirementReasons": "list",
    "IsRequiredToAttend": "bool",
    "IsInGoodStanding": "bool",
    "Status": "string",
}

#: Every ledger-row key the app reads. ChapelDate and CreatedAt are
#: timestamps or null, which is why they are "string|null".
LEDGER_FIELDS = {
    "Count": "number",
    "ChapelDate": "string|null",
    "EntryType": "string",
    "CreatedReason": "string",
    "CreatedAt": "string|null",
    "CanRemove": "bool",
}


def json_kind(value: object) -> str:
    """The JSON type of a decoded value, in the vocabulary of the tables above."""
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "bool"
    if isinstance(value, (int, float)):
        return "number"
    if isinstance(value, str):
        return "string"
    if isinstance(value, list):
        return "list"
    return "object"


def fixture_slug(path: str) -> str:
    """The fixture filename stem for a path, as FixtureTransport computes it."""
    parsed = urlsplit(resolve(path))
    cleaned = parsed.path.strip("/")
    if f"{parsed.scheme}://{parsed.netloc}" != BASE_URL:
        cleaned = f"{parsed.netloc}/{cleaned}"
    return re.sub(r"[^A-Za-z0-9]+", "_", cleaned).strip("_").lower() or "index"


# ---------------------------------------------------------------------------
# Where the app keeps its state (src/core/session.h)
# ---------------------------------------------------------------------------

def state_dir() -> Path:
    override = os.environ.get("MYCU_STATE_DIR")
    if override:
        return Path(override)
    xdg = os.environ.get("XDG_DATA_HOME")
    return (Path(xdg) if xdg else Path.home() / ".local" / "share") / "mycu"


def profile_dir() -> Path:
    return state_dir() / "webprofile"


# ---------------------------------------------------------------------------
# The browser (src/platform/desktop.cpp)
# ---------------------------------------------------------------------------

class DesktopWeb:
    """QtWebEngine start-up, and the persistent profile."""

    name = "desktop"
    surface_qml = "WebSurfaceDesktop.qml"

    def __init__(self) -> None:
        self._profile = None

    def before_app(self) -> None:
        """Must run before QGuiApplication exists; later is a hard crash."""
        from PySide6.QtWebEngineQuick import QtWebEngineQuick

        QtWebEngineQuick.initialize()

    def configure_profile(self, storage_dir: Path) -> None:
        from PySide6.QtWebEngineQuick import QQuickWebEngineProfile

        storage_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
        profile = QQuickWebEngineProfile()
        profile.setStorageName("mycu")
        profile.setOffTheRecord(False)
        profile.setPersistentStoragePath(str(storage_dir))
        profile.setCachePath(str(storage_dir / "cache"))
        profile.setPersistentCookiesPolicy(
            QQuickWebEngineProfile.PersistentCookiesPolicy.ForcePersistentCookies
        )
        self._profile = profile

    def qml_profile(self):
        return self._profile


# ---------------------------------------------------------------------------
# Background work (src/ui/tasks.h)
# ---------------------------------------------------------------------------

_in_flight: set = set()
_in_flight_lock = threading.Lock()


def run_in_background(
    fn: Callable[[], object],
    on_done: Callable[[object], None],
    on_error: Callable[[BaseException], None],
) -> None:
    """Run ``fn`` on a pool thread; call back on the GUI thread."""
    from PySide6.QtCore import QObject, QRunnable, QThreadPool, Signal

    class Signals(QObject):
        done = Signal(object)
        failed = Signal(object)

    class Task(QRunnable):
        def __init__(self) -> None:
            super().__init__()
            self.signals = Signals()

        def run(self) -> None:
            try:
                result = fn()
            except Exception as exc:  # noqa: BLE001
                log.error("background task failed: %s\n%s", exc, traceback.format_exc())
                self.signals.failed.emit(exc)
            else:
                self.signals.done.emit(result)

    task = Task()
    # QThreadPool owns the runnable on the C++ side, but nothing keeps the
    # Python wrapper — and its signals — alive; hold it until it reports.
    with _in_flight_lock:
        _in_flight.add(task)

    def release(_: object = None) -> None:
        with _in_flight_lock:
            _in_flight.discard(task)

    task.signals.done.connect(on_done)
    task.signals.failed.connect(on_error)
    task.signals.done.connect(release)
    task.signals.failed.connect(release)
    QThreadPool.globalInstance().start(task)


# ---------------------------------------------------------------------------
# The in-page fetch (src/ui/webviewtransport.cpp)
# ---------------------------------------------------------------------------

POLL_INTERVAL_MS = 60
DEFAULT_TIMEOUT_S = 30.0

CORS_FAILURE_MARKERS = ("failed to fetch", "networkerror", "load failed", "cors")

FETCH_SCRIPT = """
(function () {
  window.__mycu = window.__mycu || {};
  window.__mycu[%(token)s] = null;
  fetch(%(url)s, {
    credentials: 'same-origin',
    redirect: 'follow',
    headers: %(headers)s
  })
    .then(function (r) {
      return r.text().then(function (t) {
        return {
          status: r.status,
          url: r.url,
          redirected: r.redirected,
          contentType: r.headers.get('content-type') || '',
          body: t
        };
      });
    })
    .catch(function (e) { return { error: String(e) }; })
    .then(function (r) { window.__mycu[%(token)s] = JSON.stringify(r); });
})();
"""

READ_SCRIPT = """
(function () {
  if (!window.__mycu) { return ''; }
  var v = window.__mycu[%(token)s];
  if (v) { delete window.__mycu[%(token)s]; return v; }
  return '';
})();
"""


def looks_like_cors_failure(error: str) -> bool:
    lowered = (error or "").lower()
    return any(marker in lowered for marker in CORS_FAILURE_MARKERS)


def _make_transport_class():
    """Defined lazily so importing devkit does not import PySide6."""
    from PySide6.QtCore import QMetaObject, QObject, Qt, QThread, QTimer, Signal, Slot

    @dataclass
    class _Pending:
        token: str
        path: str
        done: threading.Event = field(default_factory=threading.Event)
        payload: dict | None = None
        error: str = ""
        started_at: float = field(default_factory=time.monotonic)
        timeout_s: float = DEFAULT_TIMEOUT_S

    class WebViewTransport(QObject):
        """Fetch through the WebView. Construct on the GUI thread; call get()
        and evaluate() from worker threads."""

        sessionExpired = Signal()
        _startRequested = Signal(str, str)
        _evalRequested = Signal(str, str)

        def __init__(self, parent: QObject | None = None) -> None:
            super().__init__(parent)
            self._surface = None
            self._pending: dict[str, _Pending] = {}
            self._raw_tokens: set[str] = set()
            self._lock = threading.Lock()
            self._timer = QTimer(self)
            self._timer.setInterval(POLL_INTERVAL_MS)
            self._timer.timeout.connect(self._poll)
            self._startRequested.connect(self._start_on_gui, Qt.ConnectionType.QueuedConnection)
            self._evalRequested.connect(self._eval_on_gui, Qt.ConnectionType.QueuedConnection)

        def attach_surface(self, surface) -> None:
            self._surface = surface
            surface.evalResult.connect(self._on_eval_result)

        @property
        def current_url(self) -> str:
            if self._surface is None:
                return ""
            return str(self._surface.property("currentUrl") or "")

        def get(self, path: str, timeout_s: float = DEFAULT_TIMEOUT_S) -> Response:
            if self._surface is None:
                raise TransportError("no web surface attached")
            if QThread.currentThread() is self.thread():
                raise TransportError("get() on the GUI thread would deadlock; use run_in_background")

            url = resolve(path)
            surface_url = self.current_url
            if surface_url and looks_like_login(surface_url):
                raise SessionExpired(f"web surface is on a sign-in page ({surface_url})")

            token = uuid.uuid4().hex
            pending = _Pending(token=token, path=url, timeout_s=timeout_s)
            with self._lock:
                self._pending[token] = pending
            script = FETCH_SCRIPT % {
                "token": json.dumps(token), "url": json.dumps(url), "headers": "{}",
            }
            self._startRequested.emit(token, script)

            if not pending.done.wait(timeout=timeout_s + 2.0):
                with self._lock:
                    self._pending.pop(token, None)
                raise TransportError(f"timed out after {timeout_s:.0f}s waiting for {url}")
            with self._lock:
                self._pending.pop(token, None)

            if pending.error:
                if looks_like_cors_failure(pending.error):
                    where = self._page_location()
                    if where and looks_like_login(where):
                        self.sessionExpired.emit()
                        raise SessionExpired(f"{url}: the browser is on a sign-in page ({where})")
                raise TransportError(f"fetch failed for {url}: {pending.error}")

            payload = pending.payload or {}
            response = Response(
                status=int(payload.get("status") or 0),
                url=str(payload.get("url") or url),
                body=str(payload.get("body") or ""),
                headers={"content-type": str(payload.get("contentType") or "")},
            )
            if looks_like_login(response.url, response.body):
                self.sessionExpired.emit()
                raise SessionExpired(f"{url} redirected to {response.url}")
            if not response.ok:
                raise TransportError(f"{url} returned HTTP {response.status}")
            return response

        def _page_location(self) -> str:
            try:
                return str(self.evaluate("window.location.href", timeout_s=5.0) or "")
            except Exception:  # noqa: BLE001
                return ""

        def evaluate(self, script: str, timeout_s: float = 15.0) -> object:
            if self._surface is None:
                raise TransportError("no web surface attached")
            if QThread.currentThread() is self.thread():
                raise TransportError("evaluate() on the GUI thread would deadlock")

            token = uuid.uuid4().hex
            pending = _Pending(token=token, path="<evaluate>", timeout_s=timeout_s)
            with self._lock:
                self._pending[token] = pending
                self._raw_tokens.add(token)
            self._evalRequested.emit(token, script)
            try:
                if not pending.done.wait(timeout=timeout_s + 2.0):
                    raise TransportError(f"JavaScript evaluation timed out after {timeout_s:.0f}s")
            finally:
                with self._lock:
                    self._pending.pop(token, None)
                    self._raw_tokens.discard(token)
            if pending.error:
                raise TransportError(pending.error)
            return (pending.payload or {}).get("value")

        @Slot(str, str)
        def _eval_on_gui(self, token: str, script: str) -> None:
            if self._surface is None:
                self._fail(token, "web surface disappeared")
                return
            try:
                self._eval(token, script)
            except Exception as exc:  # noqa: BLE001
                self._fail(token, f"could not evaluate script: {exc}")

        @Slot(str, str)
        def _start_on_gui(self, token: str, script: str) -> None:
            if self._surface is None:
                self._fail(token, "web surface disappeared before the request started")
                return
            try:
                self._eval("", script)
            except Exception as exc:  # noqa: BLE001
                self._fail(token, f"could not evaluate fetch script: {exc}")
                return
            if not self._timer.isActive():
                self._timer.start()

        def _eval(self, token: str, script: str) -> None:
            evaluator = getattr(self._surface, "evalAsync", None)
            if callable(evaluator):
                evaluator(token, script)
                return
            QMetaObject.invokeMethod(
                self._surface, "evalAsync", Qt.ConnectionType.DirectConnection, token, script
            )

        @Slot()
        def _poll(self) -> None:
            with self._lock:
                tokens = [t for t in self._pending if t not in self._raw_tokens]
            if not tokens:
                self._timer.stop()
                return
            now = time.monotonic()
            for token in tokens:
                with self._lock:
                    pending = self._pending.get(token)
                if pending is None:
                    continue
                if now - pending.started_at > pending.timeout_s:
                    self._fail(token, f"no response within {pending.timeout_s:.0f}s")
                    continue
                try:
                    self._eval(token, READ_SCRIPT % {"token": json.dumps(token)})
                except Exception as exc:  # noqa: BLE001
                    self._fail(token, f"poll failed: {exc}")

        @Slot(str, object)
        def _on_eval_result(self, token: str, result: object) -> None:
            if not token:
                return
            with self._lock:
                is_raw = token in self._raw_tokens
            if is_raw:
                self._finish(token, payload={"value": result})
                return
            if not result:
                return
            try:
                payload = json.loads(str(result))
            except (json.JSONDecodeError, TypeError):
                self._fail(token, f"malformed result payload: {str(result)[:120]!r}")
                return
            if isinstance(payload, dict) and payload.get("error"):
                message = str(payload["error"])
                if "Failed to fetch" in message or "NetworkError" in message:
                    self._finish(token, payload={"status": 0, "url": "", "body": ""},
                                 error=f"{message} (often means the session expired)")
                else:
                    self._fail(token, message)
                return
            self._finish(token, payload=payload if isinstance(payload, dict) else {})

        def _finish(self, token: str, payload: dict, error: str = "") -> None:
            with self._lock:
                pending = self._pending.get(token)
            if pending is None:
                return
            pending.payload = payload
            pending.error = error
            pending.done.set()

        def _fail(self, token: str, message: str) -> None:
            log.warning("request %s failed: %s", token[:8], message)
            self._finish(token, payload={}, error=message)

    return WebViewTransport


def _make_login_class():
    from PySide6.QtCore import QObject, Signal

    class Login(QObject):
        """The sign-in state machine, driven by the surface's URL alone."""

        loggedIn = Signal()
        navigateRequested = Signal(str)

        def __init__(self) -> None:
            super().__init__()
            self.signing_in = False

        def begin(self, return_path: str) -> None:
            self.navigateRequested.emit(resolve(return_path or "/"))

        def onUrlChanged(self, url: str) -> None:  # noqa: N802 - mirrors the C++ slot
            if not url:
                return
            if looks_like_login(url):
                self.signing_in = True
                return
            if is_selfservice(url):
                self.signing_in = False
                self.loggedIn.emit()

    return Login


_LAZY = {"WebViewTransport": _make_transport_class, "Login": _make_login_class}


def __getattr__(name: str):
    # `from devkit import WebViewTransport` works, but PySide6 is imported only
    # when a script actually asks for a Qt-backed class — and each class is
    # built once, so every import sees the same one.
    if name in _LAZY:
        value = _LAZY[name]()
        globals()[name] = value
        return value
    raise AttributeError(name)


# ---------------------------------------------------------------------------
# A surface in a window, for scripts
# ---------------------------------------------------------------------------

def surface_window_qml(surface_qml: str, *, title: str, width: int, height: int,
                       header: str = "", extra: str = "") -> str:
    """QML for a window holding the app's own web surface, found by objectName."""
    return f"""
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "{QML_DIR.as_uri()}" as Surfaces

ApplicationWindow {{
    id: win
    visible: true
    width: {width}; height: {height}
    title: {json.dumps(title)}
    {extra}
    {header}
    Surfaces.{Path(surface_qml).stem} {{ objectName: "surface"; anchors.fill: parent }}
}}
"""
