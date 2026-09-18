"""The WebView transport: make the browser fetch it, and take the body back.

.. rubric:: Why this exists

We need authenticated requests to ``selfservice.cedarville.edu``. The session
lives in an ``HttpOnly`` ASP.NET cookie. On Android, Qt cannot read it — the
``cookieAdded`` signal fires only for cookies *we* set — so the obvious design
(harvest the cookie, drive an ``httpx`` client) is simply unavailable.

The way around it is to not need the cookie. The WebView already has it and
attaches it automatically. So we evaluate a ``fetch()`` **inside the logged-in
page**, let the browser do the request with its own credentials, and read the
body back out. The only APIs involved are ``url`` and ``runJavaScript``, both
present on QtWebEngine *and* QtWebView. That is what makes the Android port
wiring rather than research.

.. rubric:: Why it polls

``runJavaScript`` returns the value of the last expression; it cannot await a
promise, and ``fetch`` is a promise. So the script is fire-and-forget: it parks
its result on ``window.__mycu[token]``, and we poll that slot with cheap
follow-up evaluations until it is populated. Verbose, but it is the shape that
was verified working (287 KB of body came back intact), and it is identical on
both backends.

The ``.catch`` in the script is load-bearing. An unauthenticated fetch redirects
cross-origin to Microsoft and throws a CORS ``TypeError``; without the catch the
result slot is never written and the request hangs until timeout. That is
exactly what the first run of this experiment did.

.. rubric:: Threading

:meth:`WebViewTransport.get` is **synchronous and must be called from a worker
thread**. Providers stay simple and testable that way. The JavaScript is posted
to the GUI thread through a queued signal, and the calling thread blocks on an
:class:`threading.Event` until the GUI thread deposits a result. Calling
:meth:`get` from the GUI thread would deadlock — it raises
:class:`~mycu.core.errors.TransportError` instead of hanging.
"""

from __future__ import annotations

import json
import logging
import threading
import time
import uuid
from dataclasses import dataclass, field

from PySide6.QtCore import QMetaObject, QObject, Qt, QThread, QTimer, Signal, Slot

from ..core.errors import SessionExpired, TransportError
from ..core.transport import Response, looks_like_login, resolve

log = logging.getLogger(__name__)

#: How often the GUI thread checks whether the in-page fetch has finished.
#: 60 ms is well under human perception and costs a trivial JS evaluation.
POLL_INTERVAL_MS = 60

#: How long a single request may take before we give up. Generous, because the
#: first request after a cold start can involve a full SAML round trip.
DEFAULT_TIMEOUT_S = 30.0

#: How a browser reports a request it refused to make, as opposed to one that
#: came back with an error status. Chromium says "TypeError: Failed to fetch"
#: for a CORS refusal; other engines word it differently, hence the set. A
#: request that was *refused* is the signature of the page having been
#: redirected off our origin — i.e. of a sign-in — so it gets a second look.
CORS_FAILURE_MARKERS = (
    "failed to fetch",          # Chromium / the Android system WebView
    "networkerror",             # Gecko
    "load failed",              # WebKit
    "cors",
)


def _looks_like_cors_failure(error: str) -> bool:
    lowered = (error or "").lower()
    return any(marker in lowered for marker in CORS_FAILURE_MARKERS)

#: The fetch, in the page's own origin.
#:
#: ``credentials: 'same-origin'`` is what makes the browser attach the session
#: cookie. ``redirect: 'follow'`` is the default and is what lets ``r.url``
#: reveal that we ended up at Microsoft — which is how expiry is detected.
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

#: Reads and consumes the result slot. Returns ``""`` while still pending, so
#: the Python side can test truthiness without worrying about ``null``/``undefined``
#: marshalling differences between the two backends.
READ_SCRIPT = """
(function () {
  if (!window.__mycu) { return ''; }
  var v = window.__mycu[%(token)s];
  if (v) { delete window.__mycu[%(token)s]; return v; }
  return '';
})();
"""


@dataclass
class _Pending:
    """One in-flight request, shared between the worker and GUI threads."""

    token: str
    path: str
    done: threading.Event = field(default_factory=threading.Event)
    payload: dict | None = None
    error: str = ""
    started_at: float = field(default_factory=time.monotonic)
    timeout_s: float = DEFAULT_TIMEOUT_S


class WebViewTransport(QObject):
    """A :class:`~mycu.core.transport.Transport` backed by the WebView.

    Construct it on the GUI thread, hand it the QML web surface with
    :meth:`attach_surface`, then call :meth:`get` from worker threads.
    """

    #: Emitted (GUI thread) when a request lands on an identity provider. The
    #: app uses this to raise the login surface without waiting for the worker
    #: thread to propagate the exception.
    sessionExpired = Signal()

    #: Internal: carries a request from a worker thread to the GUI thread.
    _startRequested = Signal(str, str)

    #: Internal: carries a bare JavaScript evaluation to the GUI thread.
    _evalRequested = Signal(str, str)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._surface: QObject | None = None
        self._pending: dict[str, _Pending] = {}
        #: Tokens belonging to :meth:`evaluate` rather than :meth:`get`. Their
        #: results are raw JS values, not the JSON envelope the fetch script
        #: builds, so :meth:`_on_eval_result` must not try to decode them.
        self._raw_tokens: set[str] = set()
        self._lock = threading.Lock()
        self._extra_headers: dict[str, str] = {}

        self._timer = QTimer(self)
        self._timer.setInterval(POLL_INTERVAL_MS)
        self._timer.timeout.connect(self._poll)

        # Queued so that a call from any worker thread lands on the GUI thread,
        # which is the only thread allowed to touch the WebView.
        self._startRequested.connect(self._start_on_gui, Qt.ConnectionType.QueuedConnection)
        self._evalRequested.connect(self._eval_on_gui, Qt.ConnectionType.QueuedConnection)

    # ------------------------------------------------------------------
    # Wiring
    # ------------------------------------------------------------------

    def attach_surface(self, surface: QObject) -> None:
        """Bind to the QML web surface.

        ``surface`` is whichever of ``WebSurfaceDesktop.qml`` /
        ``WebSurfaceAndroid.qml`` got loaded. Both expose the same three
        members, and this class uses nothing else:

        ``evalAsync(token, script)``
            Run JavaScript; deliver the result via ``evalResult``.
        ``evalResult(token, result)``
            Signal carrying the value back.
        ``currentUrl``
            The page the surface is showing right now.
        """
        self._surface = surface
        surface.evalResult.connect(self._on_eval_result)
        log.info("transport attached to %s", type(surface).__name__)

    def set_extra_headers(self, headers: dict[str, str]) -> None:
        """Add request headers to every fetch.

        Empty by default, on purpose. It is tempting to send
        ``X-Requested-With: XMLHttpRequest`` because ASP.NET MVC uses it to
        decide between a full page and a partial/JSON response — but that
        *changes what comes back*, and until M0 shows which form the chapel page
        serves, changing it would be guessing at two unknowns at once. Set it
        deliberately once you know.
        """
        self._extra_headers = dict(headers)

    @property
    def current_url(self) -> str:
        if self._surface is None:
            return ""
        return str(self._surface.property("currentUrl") or "")

    # ------------------------------------------------------------------
    # Transport protocol
    # ------------------------------------------------------------------

    def get(self, path: str, timeout_s: float = DEFAULT_TIMEOUT_S) -> Response:
        """Fetch ``path`` through the WebView. Blocks; call from a worker thread.

        Raises :class:`SessionExpired` if the request lands on an identity
        provider, and :class:`TransportError` for everything else.
        """
        if self._surface is None:
            raise TransportError("no web surface attached; the UI is not ready yet")

        if QThread.currentThread() is self.thread():
            raise TransportError(
                "WebViewTransport.get() was called on the GUI thread. It blocks "
                "waiting for that same thread to run JavaScript, so it would "
                "deadlock. Run providers through mycu.ui.tasks.run_in_background()."
            )

        url = resolve(path)

        # If the surface is already sitting on a sign-in page, the fetch would
        # fail on CORS and we would report a confusing transport error. Say the
        # true thing instead.
        surface_url = self.current_url
        if surface_url and looks_like_login(surface_url):
            raise SessionExpired(f"web surface is on a sign-in page ({surface_url})")

        token = uuid.uuid4().hex
        pending = _Pending(token=token, path=url, timeout_s=timeout_s)
        with self._lock:
            self._pending[token] = pending

        script = FETCH_SCRIPT % {
            "token": json.dumps(token),
            "url": json.dumps(url),
            "headers": json.dumps(self._extra_headers),
        }
        self._startRequested.emit(token, script)

        if not pending.done.wait(timeout=timeout_s + 2.0):
            with self._lock:
                self._pending.pop(token, None)
            raise TransportError(
                f"timed out after {timeout_s:.0f}s waiting for {url}. The page "
                "may still be loading, or the WebView may have been destroyed."
            )

        with self._lock:
            self._pending.pop(token, None)

        if pending.error:
            # A fetch that fails outright — no status, no body — is very often
            # not a network fault but a sign-in in disguise: the page has been
            # redirected to the identity provider, and the browser refuses a
            # cross-origin request back to Self-Service. Chromium reports that
            # as a bare `TypeError: Failed to fetch`, which this used to pass
            # straight through as a transport error. The user got "Couldn't
            # reach Self-Service" and no way to sign in.
            #
            # `current_url` is not trustworthy enough to settle it — on
            # QtWebView it can still hold the pre-redirect URL (see
            # WebSurfaceAndroid.qml) — so ask the page where it actually is.
            # This costs one round trip and only on the failure path.
            if _looks_like_cors_failure(pending.error):
                where = self._page_location()
                if where and looks_like_login(where):
                    self.sessionExpired.emit()
                    raise SessionExpired(
                        f"{url} could not be fetched because the browser is on "
                        f"a sign-in page ({where}); the session has ended"
                    )
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
            raise SessionExpired(
                f"{url} redirected to {response.url}; the Self-Service session "
                "has ended"
            )

        if not response.ok:
            raise TransportError(f"{url} returned HTTP {response.status}")

        log.debug("fetched %s -> %d (%d bytes)", url, response.status, len(response.body))
        return response

    def _page_location(self) -> str:
        """Where the browser actually is, asked of the page itself.

        ``window.location.href`` is always the truth, where the surface's
        ``currentUrl`` property is only as good as the signals the backend
        emits. Used on the failure path to tell "the session ended" apart from
        "the network is down", so it must never raise: if we cannot ask, we
        simply do not know, and the caller reports the original error.
        """
        try:
            return str(self.evaluate("window.location.href", timeout_s=5.0) or "")
        except Exception as exc:  # noqa: BLE001
            log.debug("could not read the page location: %r", exc)
            return ""

    def evaluate(self, script: str, timeout_s: float = 15.0) -> object:
        """Run JavaScript in the current page and return its value.

        The value of the script's **last expression**, marshalled back through
        Qt — so strings, numbers, booleans and (on QtWebEngine) plain objects
        work, but a promise does not. That limitation is the whole reason
        :meth:`get` polls instead of awaiting; see this module's docstring.

        Blocks, so like :meth:`get` it must be called from a worker thread.

        This is what makes discovery possible without guessing: a page's own
        network activity is readable with

        .. code-block:: javascript

            performance.getEntriesByType('resource').map(e => e.name)

        which lists every URL the page fetched — including the XHR that filled
        a tab you cannot find in the served HTML.
        """
        if self._surface is None:
            raise TransportError("no web surface attached")

        if QThread.currentThread() is self.thread():
            raise TransportError(
                "evaluate() blocks on the GUI thread and would deadlock; "
                "call it from a worker thread"
            )

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

    # ------------------------------------------------------------------
    # GUI-thread internals
    # ------------------------------------------------------------------

    @Slot(str, str)
    def _eval_on_gui(self, token: str, script: str) -> None:
        """Run a bare evaluation. Always on the GUI thread."""
        if self._surface is None:
            self._fail(token, "web surface disappeared")
            return
        try:
            self._eval(token, script)
        except Exception as exc:  # noqa: BLE001
            self._fail(token, f"could not evaluate script: {exc}")

    @Slot(str, str)
    def _start_on_gui(self, token: str, script: str) -> None:
        """Kick off the in-page fetch. Always runs on the GUI thread."""
        if self._surface is None:
            self._fail(token, "web surface disappeared before the request started")
            return

        try:
            self._eval(token="", script=script)  # fire and forget
        except Exception as exc:  # noqa: BLE001 - surface any Qt error as ours
            self._fail(token, f"could not evaluate fetch script: {exc}")
            return

        if not self._timer.isActive():
            self._timer.start()

    def _eval(self, token: str, script: str) -> None:
        """Call ``evalAsync`` on the QML surface.

        A QML-declared function is exposed in the object's meta-object, so a
        direct Python call normally works; ``QMetaObject.invokeMethod`` is the
        documented fallback for the cases where PySide6 does not synthesise the
        attribute.
        """
        surface = self._surface
        assert surface is not None

        evaluator = getattr(surface, "evalAsync", None)
        if callable(evaluator):
            evaluator(token, script)
            return

        QMetaObject.invokeMethod(
            surface,
            "evalAsync",
            Qt.ConnectionType.DirectConnection,
            token,
            script,
        )

    @Slot()
    def _poll(self) -> None:
        """Ask the page whether any pending result has landed."""
        with self._lock:
            # Bare evaluations answer through their own callback and must never
            # be polled with the fetch-result read script.
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
        """Receive a polled value from the QML surface.

        ``token`` is empty for the fire-and-forget kickoff evaluation, which has
        no result worth looking at.
        """
        if not token:
            return

        with self._lock:
            is_raw = token in self._raw_tokens

        if is_raw:
            # A bare evaluate(): the value is whatever the script returned, and
            # `None`/empty is a legitimate answer rather than "still pending".
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
            # A cross-origin redirect to the IdP surfaces here as a CORS
            # TypeError. That *is* an expired session, so translate it rather
            # than reporting an opaque fetch failure.
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
