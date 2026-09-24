# Architecture

CedarView is C++ (Qt 6.11) with a QML interface. Two seams carry the whole
design:

1. **The core is GUI-free.** Everything under `src/core/` links Qt Core and
   nothing else.
2. **Nothing above the transport knows how the request was made.**

Together they make Android a packaging exercise rather than a port, and let the
full test suite run with no display and no network. Both are enforced, not just
documented: `src/core/CMakeLists.txt` refuses to configure if `cedarview_core`
links anything but `Qt6::Core` (so a GUI header cannot even be found from the
core), and `tests/tst_core_is_gui_free.cpp` greps the sources for
module-qualified includes that would slip past the include paths.

```
cedarview/
  CMakeLists.txt            the build, the QML module, the Android packaging;
                            project(VERSION) is the single version source
  flake.nix                 devShell (desktop) + FHS env (Android packaging)
  src/
    core/                   ── links Qt Core only. No GUI below here. ──
      transport.{h,cpp}     Transport, Response, expiry detection, routing,
                            FixtureTransport
      httptransport.{h,cpp} plain HTTPS for the public services (its own
                            library: Qt Network on desktop, JNI on Android)
      session.{h,cpp}       persistence of non-secret metadata
      models.{h,cpp}        the domain types the viewmodels expose
      errors.h              SessionExpired / TransportError / ParseError
      calendar.{h,cpp}      term start/end dates — the one hand-entered fact
      htmlattrs.{h,cpp}     one element's attributes from the meal-plan page
      pyjson.{h,cpp}        defensive reads of someone else's JSON
      providers/            chapel, chapel_schedule, dining, meals
    ui/
      main.cpp              assembly + startup ordering
      webviewtransport.*    Transport impl: in-page fetch via runJavaScript
      login.*               the auth state machine
      settings.*            UI preferences (QSettings); owns the theme flag
      tasks.h               run blocking work off the GUI thread
      bridge.*              odds and ends QML needs
      viewmodels/           C++ <-> QML
    platform/               ← the only platform-specific code
      backend.h             the interface
      desktop.cpp           QtWebEngine
      android.cpp           QtWebView
      android_sessiontransport.*  Self-Service over HTTPS with the WebView's cookies
  qml/                      shared verbatim, desktop and Android
    Main.qml                ribbon header, tab stack, bottom bar
    SummaryView.qml         the screen the app opens on
    Theme.qml               both palettes; nothing else hard-codes a colour
    Glyph.qml               every icon, drawn on a Canvas — see below
    WebSurface*.qml         the browser, one per platform, plus a stub
    icon.png                the header logo, and the only raster asset
  android/                  the manifest, the adaptive icon, HttpGet.java (both HTTPS paths)
  tests/                    Qt Test suites under ctest, fixtures, no network
  scripts/                  build-apk, and the desktop dev tools (Python)
```

---

## The transport seam

```cpp
class Transport {
public:
    virtual Response get(const QString &path) = 0;
};
```

`providers/chapel.cpp` calls `transport->get("/cedarinfo/chapelskip")` and
parses a string. It has no idea a browser is involved, which is why it is
testable against saved fixtures with no GUI and no network.

The implementations:

| Implementation | Used by | Needs |
|---|---|---|
| `FixtureTransport` | tests, `--demo` | a directory of files |
| `WebViewTransport` | Self-Service, on both platforms | the web surface |
| `HttpTransport` | the dining menu and chapel schedule APIs | nothing — they are public |
| `TransportRouter` | the app | the above, by origin |

`WebViewTransport` is *the same class* on desktop and Android. Only which QML
surface gets loaded differs.

Routing by origin is a correctness requirement, not an optimisation: an in-page
`fetch()` is bound by the same-origin policy, so a WebView parked on
Self-Service could not reach the dining API even if we wanted it to.

### Why the WebView makes the request

We need authenticated requests. The session lives in an `HttpOnly` ASP.NET
cookie. On Android, Qt cannot read it — from the Qt docs, verbatim:

> When Qt WebEngine module is used as backend, `cookieAdded` signal will be
> emitted for any cookie added to the underlying `QWebEngineCookieStore`,
> including those added by websites. **In other cases `cookieAdded` signal is
> only emitted for cookies explicitly added with `setCookie()`.**

So harvesting the cookie and driving an HTTP client is simply unavailable on
the target platform. The way around it is to not need the cookie: the WebView
already has it and attaches it automatically, so we evaluate a `fetch()` inside
the logged-in page and read the body back out through `runJavaScript`. The only
APIs involved are `url` and `runJavaScript`, both present on both backends.
Verified working: 287 KB of response body came back intact.

**Android no longer does this** (since v0.2.1). QtWebView 6.11 calls a
`runJavaScript` callback from `evaluateJavascript`'s `ValueCallback`, on the
Android UI thread, and never moves it to Qt's GUI thread
(`qandroidwebview.cpp` `javaScriptResult()` → `qquickwebview.cpp`
`QJSValue::call`). So the QML callback in `evalAsync` ran alongside the GUI
thread's own use of the QML engine, and corrupted it at random. v0.2.0 closed on
most refreshes, and the tombstones show SIGSEGV in `libQt6Qml` on the Android
main thread.

The cookie is reachable after all, just not through Qt: `android.webkit.CookieManager`
is the WebView's own cookie jar, shared by the whole process, HttpOnly cookies
included. So on Android `main.cpp` routes Self-Service to
`AndroidSessionTransport` (`src/platform/android_sessiontransport.*`). That calls
`HttpGet.getWithWebViewCookies()`, which sends the WebView's cookies and User-Agent,
writes back any cookie the server sets, and follows redirects only within
Self-Service. A redirect to Microsoft stops there and becomes `SessionExpired`,
which reopens the sign-in surface just as before. The WebView still does the
signing in. It just never runs a script.

Do not route Android back through `WebViewTransport` until QtWebView delivers
`runJavaScript` results on the GUI thread. A bridge that called `evaluateJavascript`
itself was tried and cannot work: Qt detaches the WebView from the view tree
whenever the sign-in surface is hidden and keeps no reachable reference to it.

### Why it polls

`runJavaScript` returns the value of the last expression. It cannot await a
promise, and `fetch` is a promise. So the script is fire-and-forget — it parks
its result on `window.__mycu[token]` — and the transport polls that slot with
cheap follow-up evaluations at 60 ms until it is populated.

The `.catch` in the script is load-bearing. An unauthenticated fetch redirects
cross-origin and throws a CORS `TypeError`; without the catch the result slot is
never written and the request hangs until timeout.

### Threading

`WebViewTransport::get()` is synchronous and **must be called from a worker
thread** — that is what keeps providers simple. The JavaScript is posted to the
GUI thread with a queued call; the caller blocks on a condition variable.
Calling it from the GUI thread would deadlock, so it throws `TransportError`
with an explanation instead of hanging.

```
worker thread                    GUI thread
─────────────                    ──────────
get(path)
  invokeMethod(queued)  ───────▶ startOnGui: evalAsync(fetch…)
  cv.wait()                      QTimer 60 ms ──▶ evalAsync(read slot)
                                      │
                                      ▼
                        onEvalResult: parse, notify
  ◀──────────────────────────────┘
return Response
```

`runInBackground()` (`src/ui/tasks.h`) is the one place work goes to the pool.
Results and exceptions come back as queued calls on the GUI thread, through a
relay object — never to the requesting object directly, because it may have
been destroyed while the work ran, and a queued call aimed at a dead object is
a crash.

### Plain HTTPS on Android

`HttpTransport` is `QNetworkAccessManager` on the desktop. On Android it calls
`android/src/com/kromakobra/cedarview/HttpGet.java` over JNI instead. Qt has no
native TLS backend on Android, so Qt Network there would need an OpenSSL — and
a CA list — bundled into the APK; a bundled CA list goes stale and would keep
trusting a CA the phone's owner had distrusted. `HttpsURLConnection` uses the
phone's own trust store, so "trusted" means exactly what it means everywhere
else on the device.

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

The control flow is exception-driven on purpose: each viewmodel's failure
handler is a catch ladder over `SessionExpired`, `ParseError` and
`TransportError`, and each becomes different copy on screen.

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
browser at all, which is what `tests/tst_login_flow.cpp` does.

---

## The screens

Four tabs, behind a bottom bar: **Summary**, **Chapel**, **Dining** and
**Chucks**.

Summary is deliberately *not* owned by one viewmodel. It reads from `chapel`,
`dining` and `semester`, and through them from four separate services — the
skip ledger and the meal-plan page (behind the Cedarville sign-in), the chapel
schedule and the dining menu API (public). The two things a student checks in
the morning used to be one tab apart; putting them on one card stack is the
whole point of the screen.

A consequence worth knowing: the public half fills in before you sign in and
stays filled in after you sign out, so the screen is never entirely blank.

The viewmodels are exposed as context properties (`bridge`, `login`, `chapel`,
`dining`, `semester`, `settings`), which QML resolves by name at runtime. A
typo is therefore not a compile error; `tests/tst_qml_contract.cpp` reads every
`chapel.<name>`-style reference out of the QML and checks it against the
classes' `staticMetaObject`, and every `model.<role>` against the list models'
role names.

Two rules the screen is built on, both of which have bitten this codebase:

* **No confident zeros.** Every figure the app has not actually received
  renders as an em dash. `$0.00` and `0 skips left` are the two most alarming
  things this app could say and it must never say either by accident — which is
  why the core uses `std::optional` and invalid dates for "not reported", and
  the viewmodels translate those to `-1` and `""` for QML, which has no null.
* **No glyphs from fonts.** Every icon is drawn on a `Canvas` in `Glyph.qml`.
  The toolbar once used "↻" (U+21BB), which the desktop font has and the
  phone's Roboto does not, so it rendered as a tofu box on the moto g power. An
  icon that fails to render in a tab bar leaves the user with no idea what the
  tab is. Canvas is part of QtQuick proper — no font, no QtSvg.

  The header logo is the one exception: it is `qml/icon.png`, a raster PNG,
  which satisfies the same rule for the same reason — nothing about it depends
  on what the device has installed.

The QML is one module, `CedarView`, compiled ahead of time by qmlcachegen and
embedded in the binary (`qt_add_qml_module` in `CMakeLists.txt`). A new QML file
goes in that list. Each platform's build gets only its own web surface —
`WebSurfaceDesktop.qml` imports QtWebEngine, which does not exist on Android,
and the deployment tool decides what to bundle by reading these files.

---

## Adding a provider

Grades, schedule and student account are each one more provider. The session
and transport are untouched.

1. Capture the page (`docs/discovery.md`).
2. Scrubbed body → `tests/fixtures/<slug>.json` or `.html`.
3. `src/core/providers/<name>.{h,cpp}`, with a `fetch()` over the transport
   and a parse function the tests can call directly. Add it to
   `src/core/CMakeLists.txt`.
4. `tests/tst_<name>_provider.cpp`, added to `tests/CMakeLists.txt`.
5. A viewmodel + a QML page.

Steps 1–4 need no phone, no display and no network.

---

## Desktop and Android, decided at compile time

`CMakeLists.txt` compiles `src/platform/desktop.cpp` or `android.cpp`, never
both, so a desktop binary never links QtWebView and the APK never contains
QtWebEngine. The two backends differ in exactly three things: which web engine
is initialised (both before the application object exists), which surface QML
is loaded, and whether the app configures the cookie jar (desktop: a persistent
QtWebEngine profile of its own, because Qt 6's default one is off the record;
Android: the system WebView already keeps it in app-private storage).

---

## History

Until September 2026 the app was Python: PySide6, packaged for Android with
pyside6-android-deploy, buildozer and python-for-android. The APK carried
CPython and PySide6 (150 MB), and building it needed a rebuilt 16 KB
libshiboken and some 1,400 lines of build-script workarounds. The C++ port
kept the design module for module — the same seams, names, sentinels, error
copy and QML — and the APK is now Qt and one library of ours. The design notes
above were written for the Python app and still hold.
