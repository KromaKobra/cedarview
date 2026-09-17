"""The transport seam.

A :class:`Transport` fetches a path on ``selfservice.cedarville.edu`` and hands
back a string. That is the entire contract. Providers are written against it and
therefore never learn that a WebView is involved, which is why they can be
tested against saved fixtures with no Qt and no network.

There are two implementations:

* :class:`FixtureTransport` (here, in the core) — serves files from
  ``tests/fixtures/``. Used by the test suite and by ``--demo`` mode.
* ``mycu.ui.transport_webview.WebViewTransport`` — evaluates ``fetch()`` inside
  the logged-in page. Identical code on desktop and Android.

Session expiry is *detected, not predicted*: see :func:`looks_like_login`.
"""

from __future__ import annotations

import json
import logging
import re
import threading
from dataclasses import dataclass, field
from pathlib import Path
from typing import Protocol, runtime_checkable
from urllib.parse import urljoin, urlsplit

from .errors import ParseError, SessionExpired, TransportError

#: Ellucian Colleague Self-Service. A single SAML Service Provider, so one login
#: covers chapel, grades, schedule and the student account alike. This is the
#: default origin: a bare path like ``/cedarinfo/chapelskip`` resolves here.
BASE_URL = "https://selfservice.cedarville.edu"

#: Cedarville's dining menu service. **Unauthenticated** — its own page fetches
#: it with ``credentials: "omit"``. Verified 2026-09-16: ``/api/menus?days=N``
#: returns ``{"YYYY-MM-DD": [{venue, meal, slot, items}]}``. Because there is no
#: session involved, this origin is served by :class:`HttpTransport`, not
#: through the WebView — see :class:`TransportRouter`.
DINING_BASE = "https://diningdata.cedarville.edu"

#: Hosts that mean "you are not logged in". Landing on any of these is the
#: definitive expiry signal — far more robust than tracking cookie lifetimes,
#: because Entra ID's session policy can change without telling us.
IDP_HOSTS = (
    "login.microsoftonline.com",
    "login.microsoft.com",
    "login.windows.net",
    "sts.cedarville.edu",
    "adfs.cedarville.edu",
)

#: Markers that appear in a login page body but should never appear in a
#: Self-Service data page. Used only as a fallback when the final URL is
#: unavailable or inconclusive.
LOGIN_BODY_MARKERS = (
    "SAMLRequest",
    "urn:oasis:names:tc:SAML",
    '$Config={"fShowPersistentCookiesWarning"',  # Entra ID sign-in bootstrap
    "ConvergedSignIn",
    "Sign in to your account",
)

#: Sent by the WebView-driven requests. Honest about what this is: a personal
#: client reading one student's own records.
log = logging.getLogger(__name__)

USER_AGENT_SUFFIX = "myCU/0.1 (personal student-records client)"


@dataclass(slots=True)
class Response:
    """What a transport returns.

    ``final_url`` is the load-bearing field. After a redirect chain, it is the
    only reliable way to know whether we ended up on Self-Service or got bounced
    to the identity provider.
    """

    status: int
    url: str
    body: str
    headers: dict[str, str] = field(default_factory=dict)

    @property
    def ok(self) -> bool:
        return 200 <= self.status < 300

    def json(self) -> object:
        """Parse the body as JSON, raising :class:`ParseError` on failure.

        The failure message includes a body prefix, because the single most
        common cause is an HTML login page arriving where JSON was expected.
        """
        try:
            return json.loads(self.body)
        except (json.JSONDecodeError, TypeError) as exc:
            preview = (self.body or "")[:200].replace("\n", " ")
            raise ParseError(
                f"expected JSON from {self.url!r} but got {preview!r}"
            ) from exc

    def raise_for_session(self) -> "Response":
        """Raise :class:`SessionExpired` if this response is really a login page.

        Call this before parsing. Returns ``self`` so it can be chained.
        """
        if looks_like_login(self.url, self.body):
            raise SessionExpired(
                f"request for {self.url!r} landed on an identity provider; "
                "the Self-Service session is gone"
            )
        return self


@runtime_checkable
class Transport(Protocol):
    """Fetch a Self-Service path and return the response.

    ``path`` may be absolute (``https://selfservice.cedarville.edu/...``) or
    root-relative (``/cedarinfo/chapelskip``). Implementations resolve it
    against :data:`BASE_URL`.

    Implementations raise :class:`~mycu.core.errors.SessionExpired` when the
    request lands on an identity provider, and
    :class:`~mycu.core.errors.TransportError` for anything else that goes wrong.
    """

    def get(self, path: str) -> Response:  # pragma: no cover - protocol
        ...


def resolve(path: str) -> str:
    """Turn a path into an absolute Self-Service URL.

    >>> resolve("/cedarinfo/chapelskip")
    'https://selfservice.cedarville.edu/cedarinfo/chapelskip'
    >>> resolve("https://selfservice.cedarville.edu/Student/Grades")
    'https://selfservice.cedarville.edu/Student/Grades'
    """
    if path.startswith(("http://", "https://")):
        return path
    return urljoin(BASE_URL + "/", path.lstrip("/"))


def is_selfservice(url: str) -> bool:
    """Whether ``url`` is on the Self-Service origin."""
    return url.startswith(BASE_URL)


def origin_of(url_or_path: str) -> str:
    """The scheme+host a path will actually be requested from.

    >>> origin_of("/cedarinfo/chapelskip")
    'https://selfservice.cedarville.edu'
    >>> origin_of("https://diningdata.cedarville.edu/api/menus?days=2")
    'https://diningdata.cedarville.edu'
    """
    parsed = urlsplit(resolve(url_or_path))
    return f"{parsed.scheme}://{parsed.netloc}"


def looks_like_login(url: str, body: str = "") -> bool:
    """Decide whether we are looking at a sign-in page rather than data.

    Two independent signals, checked in order of reliability:

    1. **The final URL is on a known identity provider host.** Decisive. After
       following redirects, an unauthenticated Self-Service request always ends
       at Entra ID.
    2. **The body carries SAML/Entra sign-in markers.** A fallback for the case
       where the URL is missing or the IdP round-trips us back through
       Self-Service with a form post.

    A short body is *not* treated as a signal — empty responses happen for
    unrelated reasons, and guessing would send the user to a login page they did
    not need.
    """
    # Match on the HOST, never on the whole URL string. A substring test looks
    # equivalent and is not: analytics and tracking URLs routinely carry a
    # `ref=https://login.microsoftonline.com/` parameter, and a Self-Service
    # page whose URL happened to include one would be declared expired — kicking
    # the user to a sign-in they did not need. Observed for real in a capture:
    # `t.vibe.co/pixel/s?...&ref=https://login.microsoftonline.com/` raised
    # SessionExpired.
    host = (urlsplit(url or "").hostname or "").lower()
    if host and any(host == idp or host.endswith("." + idp) for idp in IDP_HOSTS):
        return True

    if body:
        head = body[:8192]
        if any(marker in head for marker in LOGIN_BODY_MARKERS):
            return True
        # A page whose <form> posts to an IdP is a login page regardless of
        # which host served it.
        if re.search(r'<form[^>]+action="https://login\.microsoft', head, re.I):
            return True

    return False


class FixtureTransport:
    """A :class:`Transport` that serves saved response bodies from disk.

    This is how every parsing test runs: no Qt, no network, no login. It is also
    what ``python -m mycu --demo`` uses, so the UI can be developed and
    screenshotted before a phone or a real session is in the picture.

    Files are looked up by a slug derived from the path::

        /cedarinfo/chapelskip  ->  fixtures/cedarinfo_chapelskip.html
                                   fixtures/cedarinfo_chapelskip.json

    The ``.json`` extension wins if both exist, so dropping a real JSON capture
    next to the placeholder HTML is all it takes to switch the provider over.
    """

    def __init__(self, root: Path | str, *, status: int = 200) -> None:
        self.root = Path(root)
        self.status = status

    @staticmethod
    def slug(path: str) -> str:
        """Filename stem for a path.

        Paths on the default origin keep their bare form; anything else is
        prefixed with its host, so fixtures from different services cannot
        collide:

        >>> FixtureTransport.slug("/cedarinfo/chapelskip")
        'cedarinfo_chapelskip'
        >>> FixtureTransport.slug("https://diningdata.cedarville.edu/api/menus?days=2")
        'diningdata_cedarville_edu_api_menus'

        The query string is dropped deliberately — ``?days=2`` and ``?days=7``
        are the same endpoint, and a fixture per parameter value would be
        noise.
        """
        url = resolve(path)
        parsed = urlsplit(url)
        cleaned = parsed.path.strip("/")
        if f"{parsed.scheme}://{parsed.netloc}" != BASE_URL:
            cleaned = f"{parsed.netloc}/{cleaned}"
        return re.sub(r"[^A-Za-z0-9]+", "_", cleaned).strip("_").lower() or "index"

    def get(self, path: str) -> Response:
        slug = self.slug(path)
        for suffix in (".json", ".html", ".txt"):
            candidate = self.root / f"{slug}{suffix}"
            if candidate.exists():
                return Response(
                    status=self.status,
                    url=resolve(path),
                    body=candidate.read_text(encoding="utf-8"),
                    headers={
                        "content-type": (
                            "application/json" if suffix == ".json" else "text/html"
                        )
                    },
                )

        available = sorted(p.name for p in self.root.glob("*")) if self.root.exists() else []
        raise FileNotFoundError(
            f"no fixture for {path!r} (looked for {slug}.json/.html/.txt in "
            f"{self.root}); available: {available}"
        )


#: Where Android keeps its system CA certificates. Directories of files named
#: by subject hash (``01419da9.0``), which is exactly the layout OpenSSL's
#: ``capath`` expects. Ordered most-canonical-first; the first one that exists
#: and is non-empty wins. All three are usually present and identical — the
#: APEX path is the modern one, ``/etc`` is a symlink to ``/system/etc``.
ANDROID_CA_PATHS = (
    "/apex/com.android.conscrypt/cacerts",
    "/system/etc/security/cacerts",
    "/etc/security/cacerts",
)

#: Matches one PEM certificate. Android's CA files are a PEM block followed by
#: the human-readable `openssl x509 -text` dump and a fingerprint line, so the
#: blocks have to be cut out rather than the file used whole.
PEM_BLOCK = re.compile(
    r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----",
    re.DOTALL,
)

_ssl_context_cache: object | None = None
_ssl_context_lock = threading.Lock()


def ssl_context():
    """An SSL context with a working trust store, on desktop *and* Android.

    On a normal Linux box ``ssl.create_default_context()`` finds the system CA
    bundle and there is nothing to do. **On Android it silently finds nothing**
    — there is no OpenSSL default cert path in the app sandbox — so every HTTPS
    request through :class:`HttpTransport` fails with::

        [SSL: CERTIFICATE_VERIFY_FAILED] unable to get local issuer certificate

    which looks like a server or network problem and is neither. Android does
    ship a perfectly good CA store, just not where OpenSSL looks, so point the
    context at it explicitly.

    The check is "did the default context actually load any CAs", not "are we
    on Android". That keeps the desktop path untouched, needs no platform
    detection here (:mod:`mycu.core` stays platform-agnostic), and would also
    rescue a desktop box with an unusual cert layout.

    Deliberately **not** solved by adding ``certifi``: a bundled CA list goes
    stale, ships a second source of truth for trust decisions, and means this
    app would keep trusting a CA the phone's owner had distrusted. Reading the
    phone's own store means "trusted" here means exactly what it means
    everywhere else on the device.

    .. rubric:: Why the certificates are read rather than pointed at

    The obvious implementation is ``load_verify_locations(capath=...)``, since
    Android's directories use OpenSSL's hashed-filename layout (``01419da9.0``).
    It does not work, and it fails *silently*:

    * OpenSSL treats a ``capath`` as a **lazy** lookup. Nothing is loaded when
      you add it, so ``cert_store_stats()`` still reports zero CAs and there is
      no way to tell a good path from a typo.
    * The lazy lookup then did not resolve these files anyway — verification
      kept failing with all three directories registered.

    Reading the files and passing ``cadata`` is deterministic and verifiable:
    afterwards ``cert_store_stats()`` reports a real number, which is asserted
    below. Each Android CA file is a PEM block followed by a human-readable
    ``openssl x509 -text`` dump, so the blocks are extracted rather than the
    file being used whole.
    """
    global _ssl_context_cache

    with _ssl_context_lock:
        if _ssl_context_cache is not None:
            return _ssl_context_cache

        import ssl

        context = ssl.create_default_context()
        if context.cert_store_stats()["x509_ca"] == 0:
            _load_android_cas(context)

        _ssl_context_cache = context
        return context


def _load_android_cas(context) -> None:
    """Load the device's trusted CAs into ``context``, in place."""
    import ssl

    for candidate in ANDROID_CA_PATHS:
        directory = Path(candidate)
        if not directory.is_dir():
            continue

        pem: list[str] = []
        for entry in sorted(directory.iterdir()):
            try:
                pem.extend(PEM_BLOCK.findall(entry.read_text(errors="replace")))
            except OSError:
                continue  # unreadable or not a file; the rest are still good

        if not pem:
            continue

        try:
            context.load_verify_locations(cadata="\n".join(pem) + "\n")
        except ssl.SSLError as exc:
            log.warning("ssl: %s held no usable certificates: %s", candidate, exc)
            continue

        loaded = context.cert_store_stats()["x509_ca"]
        if loaded:
            log.info("ssl: loaded %d system CAs from %s", loaded, candidate)
            return

    # Fail loudly here rather than at every request: a context with no CAs
    # cannot verify anything, and the resulting per-request error blames the
    # network instead.
    log.error(
        "ssl: no CA certificates found (tried %s). HTTPS verification will "
        "fail for every request.",
        ", ".join(ANDROID_CA_PATHS),
    )


class HttpTransport:
    """A plain HTTP client, for origins that need no session.

    Used only for **unauthenticated** endpoints — currently the dining menu
    service, whose own page fetches it with ``credentials: "omit"``.

    Why this exists alongside the WebView transport: the WebView is needed only
    because the Self-Service session cookie is unreachable. Where there is no
    cookie to worry about, routing through a browser would be slower, harder to
    debug, and would need the surface parked on that origin to satisfy CORS. A
    direct request is simply the right tool.

    Built on ``urllib`` from the standard library rather than ``httpx``, so it
    is one less C-free-but-still-extra dependency for python-for-android to
    bundle. Never send credentials through this class; that is what
    ``WebViewTransport`` is for.
    """

    def __init__(self, *, timeout_s: float = 20.0, user_agent: str = "") -> None:
        self.timeout_s = timeout_s
        self.user_agent = user_agent or USER_AGENT_SUFFIX

    def get(self, path: str) -> Response:
        import urllib.error
        import urllib.request

        url = resolve(path)
        request = urllib.request.Request(
            url,
            headers={"User-Agent": self.user_agent, "Accept": "application/json, */*"},
        )

        try:
            with urllib.request.urlopen(
                request, timeout=self.timeout_s, context=ssl_context()
            ) as reply:
                body = reply.read().decode(
                    reply.headers.get_content_charset() or "utf-8", errors="replace"
                )
                return Response(
                    status=reply.status,
                    url=reply.geturl(),
                    body=body,
                    headers={"content-type": reply.headers.get("Content-Type", "")},
                )
        except urllib.error.HTTPError as exc:
            raise TransportError(f"{url} returned HTTP {exc.code}") from exc
        except Exception as exc:  # noqa: BLE001 - URLError, socket, ssl, DNS…
            raise TransportError(f"could not reach {url}: {exc}") from exc


class TransportRouter:
    """Sends each request to the transport that suits its origin.

    The app talks to more than one service, and they do not all authenticate
    the same way:

    ===================================  ==================  ================
    Origin                               Auth                Transport
    ===================================  ==================  ================
    ``selfservice.cedarville.edu``       SAML / Entra ID     WebView
    ``diningdata.cedarville.edu``        none                plain HTTP
    ===================================  ==================  ================

    Providers stay unaware of all of this: they declare a path and call
    ``transport.get(path)``, exactly as before.

    A second reason this class exists: an in-page ``fetch()`` is subject to the
    same-origin policy. A WebView parked on Self-Service *cannot* fetch the
    dining API — the browser would block it before the request left. Routing by
    origin is not an optimisation, it is a correctness requirement.
    """

    def __init__(self, default: "Transport", routes: "dict[str, Transport] | None" = None) -> None:
        self.default = default
        self.routes: dict[str, Transport] = dict(routes or {})

    def route(self, origin: str, transport: "Transport") -> "TransportRouter":
        """Send everything on ``origin`` to ``transport``. Chainable."""
        self.routes[origin.rstrip("/")] = transport
        return self

    def transport_for(self, path: str) -> "Transport":
        return self.routes.get(origin_of(path), self.default)

    def get(self, path: str) -> Response:
        return self.transport_for(path).get(path)
