# Architecture

Two seams carry the whole design:

1. **`mycu.core` never imports Qt.**
2. **Nothing above the transport knows how the request was made.**

Together they make the Android port a packaging exercise rather than a rewrite,
and let the full test suite run with no display and no network. Both are
enforced by tests, not just documented — `tests/test_core_is_qt_free.py` walks
the AST of every core module *and* re-imports the package in a subprocess with
PySide6 blocked at the meta-path.

```
cedarview/
  flake.nix                 devShell (desktop) + FHS env (Android packaging)
  .envrc                    use flake
  mycu/
    core/                   ── pure Python. No `import PySide6` below here. ──
      transport.py          Transport protocol, Response, expiry detection
      session.py            persistence of non-secret metadata
      models.py             dataclasses the QML binds to
      errors.py             SessionExpired / TransportError / ParseError
      calendar.py           term start/end dates — the one hand-entered fact
      providers/
        base.py             fetch() + parse() split
        chapel.py           /cedarinfo/chapelskip -> ChapelSummary
    ui/
      app.py                assembly + startup ordering
      transport_webview.py  Transport impl: in-page fetch via runJavaScript
      login.py              the auth state machine
      settings.py           UI preferences (QSettings); owns the theme flag
      tasks.py              run blocking work off the GUI thread
      viewmodels/           Qt <-> QML bridge
      qml/                  shared verbatim, desktop and Android
        Main.qml            ribbon header, tab stack, bottom bar
        SummaryView.qml     the screen the app opens on
        Theme.qml           both palettes; nothing else hard-codes a colour
        Glyph.qml           every icon, drawn on a Canvas — see below
        icon.png            the header logo, and the only raster asset
    platform/               ← the only Android-specific code
      desktop.py            QtWebEngine
      android.py            QtWebView
  tests/                    flat pytest, fixtures, no network
  scripts/                  check-live, build-apk
```

---

## The transport seam

```python
class Transport(Protocol):
    def get(self, path: str) -> Response: ...
```

`providers/chapel.py` calls `transport.get("/cedarinfo/chapelskip")` and parses
a string. It has no idea a browser is involved, which is why it is testable
against saved fixtures with no Qt and no network.

Three implementations exist:

| Implementation | Used by | Needs |
|---|---|---|
| `FixtureTransport` | tests, `--demo` | a directory of files |
| `WebViewTransport` (desktop) | `python -m mycu` | QtWebEngine |
| `WebViewTransport` (Android) | the APK | QtWebView |

The last two are *the same class*. Only which QML surface gets loaded differs.

### Why the WebView makes the request

We need authenticated requests. The session lives in an `HttpOnly` ASP.NET
cookie. On Android, Qt cannot read it — from the Qt docs, verbatim:

> When Qt WebEngine module is used as backend, `cookieAdded` signal will be
> emitted for any cookie added to the underlying `QWebEngineCookieStore`,
> including those added by websites. **In other cases `cookieAdded` signal is
> only emitted for cookies explicitly added with `setCookie()`.**

So harvesting the cookie and driving an `httpx` client is simply unavailable on
the target platform. The way around it is to not need the cookie: the WebView
already has it and attaches it automatically, so we evaluate a `fetch()` inside
the logged-in page and read the body back out through `runJavaScript`. The only
APIs involved are `url` and `runJavaScript`, both present on both backends.
Verified working: 287 KB of response body came back intact.

### Why it polls

`runJavaScript` returns the value of the last expression. It cannot await a
promise, and `fetch` is a promise. So the script is fire-and-forget — it parks
its result on `window.__mycu[token]` — and Python polls that slot with cheap
follow-up evaluations at 60 ms until it is populated.

The `.catch` in the script is load-bearing. An unauthenticated fetch redirects
cross-origin and throws a CORS `TypeError`; without the catch the result slot is
never written and the request hangs until timeout.

### Threading

`WebViewTransport.get()` is synchronous and **must be called from a worker
thread** — that is what keeps providers simple. The JavaScript is posted to the
GUI thread through a queued signal; the caller blocks on a `threading.Event`.
Calling it from the GUI thread would deadlock, so it raises `TransportError`
with an explanation instead of hanging.

```
worker thread                    GUI thread
─────────────                    ──────────
get(path)
  emit _startRequested  ───────▶ _start_on_gui: runJavaScript(fetch…)
  event.wait()                   QTimer 60 ms ──▶ runJavaScript(read slot)
                                      │
                                      ▼
                        _on_eval_result: parse, event.set()
  ◀──────────────────────────────┘
return Response
```

---

## Session expiry: detected, not predicted

Nothing tracks cookie lifetimes. A request is made, and then:

* if the final URL is on a known identity provider host → the session is dead;
* or if the body carries SAML / Entra sign-in markers → same;
* otherwise it is data.

`SessionExpired` propagates to the viewmodel, which signals the login
controller, which re-runs the flow. Robust against Entra policy changes nobody
can see from here. A *short* body is deliberately not a signal — empty responses
happen for unrelated reasons, and a false positive means bouncing the user to a
sign-in they did not need.

---

## Login: a state machine over URLs

`selfservice.cedarville.edu` is a SAML 2.0 Service Provider federated to Entra
ID. There is no OAuth client to register, so MSAL and every token flow are out.
Interactive login in an embedded browser is the only workable approach — and it
means **the app never sees the password**.

`RelayState` carries the return path, so there is no separate "login page"
concept at all:

1. Point the surface at the target path.
2. It loads → session is alive → the browser stays hidden.
3. It 302s to Microsoft → show the surface → sign in → SAML posts back →
   `RelayState` lands on the original path → hide the surface, continue.

Success is one condition, checked one way: the surface's URL is on the
Self-Service origin and does not look like a sign-in page. Testable with no
browser at all, which is what `tests/test_login_flow.py` does.

---

## The screens

Three tabs, behind a bottom bar. **Summary** is the one that is built; Chapel
and Dining are named in the bar and say "Coming soon!", because a map that
shows where the roads are going tells you more than one that only shows the
roads already laid.

Summary is deliberately *not* owned by one viewmodel. It reads from both
`chapel` and `dining`, and through them from four separate services — the skip
ledger and the meal-plan page (behind the Cedarville sign-in), the chapel
schedule and the dining menu API (public). The two things a student checks in
the morning used to be one tab apart; putting them on one card stack is the
whole point of the screen.

A consequence worth knowing: the public half fills in before you sign in and
stays filled in after you sign out, so the screen is never entirely blank.

Two rules the screen is built on, both of which have bitten this codebase:

* **No confident zeros.** Every figure the app has not actually received
  renders as an em dash. `$0.00` and `0 skips left` are the two most alarming
  things this app could say and it must never say either by accident — which is
  why the viewmodels use `-1` and `""` as "not reported" sentinels rather than
  falsy defaults.
* **No glyphs from fonts.** Every icon is drawn on a `Canvas` in `Glyph.qml`.
  The toolbar once used "↻" (U+21BB), which the desktop font has and the
  phone's Roboto does not, so it rendered as a tofu box on the moto g power. An
  icon that fails to render in a tab bar leaves the user with no idea what the
  tab is. Canvas is part of QtQuick proper — no font, no QtSvg (which is not in
  the APK's module list).

  The header logo is the one exception: it is `mycu/ui/qml/icon.png`, a raster
  PNG, which satisfies the same rule for the same reason — nothing about it
  depends on what the device has installed. Note *where* it lives.
  `scripts/build-apk` stages only `main.py`, `pyproject.toml` and `mycu/` into
  `android-build/`, so an image under `assets/` reaches the desktop and nothing
  else. Anything the running app loads has to be inside the package.

New QML files must be added to `qml_files` in `pysidedeploy.spec`; the Android
build lists them by hand.

---

## Adding a provider

Grades, schedule and student account are each one more module. The session and
transport are untouched.

1. Capture the page (`docs/discovery.md`).
2. Scrubbed body → `tests/fixtures/<slug>.json` or `.html`.
3. `mycu/core/providers/<name>.py` subclassing `Provider`.
4. `tests/test_<name>_provider.py`.
5. A viewmodel + a QML page.

Steps 1–4 need no phone, no display and no network.

---

## Why `mycu/platform/` and not `platform/`

A top-level `platform/` directory on `sys.path` shadows the **standard library**
`platform` module, which Qt, setuptools and python-for-android all import. The
failure appears far from its cause and is miserable to diagnose inside a
buildozer run. Nesting under `mycu` means `import platform` keeps resolving to
the stdlib everywhere. `tests/test_core_is_qt_free.py` asserts the nesting has
not been undone.
