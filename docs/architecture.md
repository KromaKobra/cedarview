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
      cache.{h,cpp}         the on-device copy of what was last loaded
      jsonfile.{h,cpp}      atomic 0600 JSON files, for both of the above
      models.{h,cpp}        the domain types the viewmodels expose
      errors.h              SessionExpired / TransportError / ParseError
      calendar.{h,cpp}      term start/end dates — hand-entered
      curfew.{h,cpp}        the curfew rule — hand-entered too
      hours.{h,cpp}         dining and building hours — hand-entered too
      htmlattrs.{h,cpp}     one element's attributes from the meal-plan page
      pyjson.{h,cpp}        defensive reads of someone else's JSON
      providers/            chapel, chapel_schedule, dining, meals
    ui/
      main.cpp              assembly + startup ordering; --demo, --shoot, --clock
      webviewtransport.*    Transport impl: in-page fetch via runJavaScript
      login.*               the auth state machine, as phases
      sync.*                when to fetch what (SyncCoordinator)
      modetransport.*       live data or sample data, switched at runtime
      demotransport.*       the sample data, redated around today
      settings.*            UI preferences (QSettings); owns the theme flag
      tasks.h               run blocking work off the GUI thread
      bridge.*              odds and ends QML needs
      viewmodels/           C++ <-> QML, one per screen, plus SourceStatus
    platform/               ← the only platform-specific code
      backend.h             the interface
      desktop.cpp           QtWebEngine
      android.cpp           QtWebView
      android_sessiontransport.*  Self-Service over HTTPS with the WebView's cookies
  qml/                      shared verbatim, desktop and Android
    Main.qml                switches on login.phase; the header, tabs, bottom bar
    WelcomeView.qml         the first-run screen; SignInView.qml, Microsoft in our chrome
    TodayView.qml           the screen the app opens on; then Chapel, Dining, Campus
    Theme.qml               both palettes and the type; nothing else hard-codes a colour
    Glyph.qml               every icon, drawn on a Canvas — see below
    WebSurface*.qml         the browser, one per platform, plus a stub
    fonts/                  Bricolage Grotesque and Instrument Sans (OFL), static
    icon.png                the logo, and the only raster asset
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
| `FixtureTransport` | tests, sample-data preview | a directory of files (`:/fixtures` in the app) |
| `Redated*Transport` | preview's menus, schedule and meal activity | a `FixtureTransport` |
| `WebViewTransport` | Self-Service, on the desktop | the web surface |
| `AndroidSessionTransport` | Self-Service, on Android | the WebView's cookies |
| `HttpTransport` | the dining menu and chapel schedule APIs | nothing — they are public |
| `TransportRouter` | the app | the above, by origin |
| `ModeTransport` | every viewmodel | a live router and a preview router, switched at runtime |

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

`SessionExpired` propagates to the viewmodel, which signals the coordinator,
which asks the login controller for a silent check (below). Robust against
Entra policy changes nobody can see from here. A *short* body is deliberately not a signal — empty responses
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

Success is the surface on the Self-Service origin, somewhere that does not
look like a sign-in page — taken from a page that has **finished loading**
there (the surface's `pageLoaded`), or, while signing in, from the address once
the surface has **come back from a Microsoft page that loaded**, so the sign-in
page goes the moment it is done. Never from the address alone: both surfaces
report the address they are sent to before Self-Service has redirected them
to Microsoft, and counting that as a sign-in ended every sign-in before it had
begun. Testable with no browser at all, which is what
`tests/tst_login_flow.cpp` does.

### Phases

Microsoft's page appears only when the user asks for it. `LoginController`
keeps the app in one of six phases, and `Main.qml` switches on its `phase`:

| Phase | What shows | How it is reached |
|---|---|---|
| `welcome` | `WelcomeView` | no session on this device; sign-out |
| `checking` | the app, saved data, personal figures "awaiting" | a session we had, on the desktop at launch; an expired session |
| `signedIn` | the app | a check or sign-in landed on Self-Service; Android at launch |
| `needsSignIn` | the app, saved data, "Your session ended" banner | a check stayed on Microsoft past the grace; a check's renewal refused by the very next fetch |
| `signingIn` | `SignInView`: the surface in CedarView's chrome | Sign in, from Welcome, the banner or preview |
| `preview` | the app on sample data, "Sample data" banner | "Look around with sample data", `--demo` |

A **silent check** points the hidden surface at Self-Service. If a
Self-Service page loads, the session is good. If it lands on Microsoft, it is given `GRACE_MS`
(5 s) to come back by itself — an Entra single-sign-on round trip does — before
the phase becomes `needsSignIn`. So a returning user whose SSO still works
never sees Microsoft at all, and one whose session really ended sees their
saved figures under a banner rather than a sign-in page. A check that reaches
neither (offline) gives up after `CHECK_TIMEOUT_MS` and lets the fetches say
what is wrong. A check that says the session is back, followed within
`RENEWAL_COOLDOWN_MS` (a minute) by a fetch that says it is not, goes to
`needsSignIn` rather than checking again — otherwise the two would take turns,
out of sight, for as long as the app was open — and the sign-in that follows
starts from an empty cookie jar.

On Android, personal data needs no page (`AndroidSessionTransport` reads the
WebView's cookies), so a launch with a past session goes straight to
`signedIn` and the first fetch is the check. A device whose session file v0.3
wrote also tries one quiet personal fetch from Welcome, because v0.3 could
lose its record of a sign-in: if the WebView kept a session, Welcome is
skipped. Nowhere else — after a sign-out that would be the app signing itself
back in, and a fresh install has nothing to find.

**Sign-out** is local and immediate: the WebView's cookie jar is emptied
(Microsoft's cookies and Self-Service's alike), `session.json` and the personal
cache are deleted, the surface is parked on `about:blank`, and the app is at
Welcome. There is no trip to Microsoft's logout page: the jar has to be emptied
for Self-Service's own session to end, and with it empty the logout page has
nothing to act on. All it ever did was leave Microsoft's sign-in page on screen
for up to fifteen seconds. The next sign-in asks for the account and password.

## Startup and sync

`SyncCoordinator` (`sync`) is the one place that decides when to fetch. At
launch:

1. The viewmodels read back what the last run saved, in their constructors,
   before QML loads — so the first frame has figures in it.
2. The public sources are fetched unless what was saved is fresh: menus under
   30 minutes old with today in them, a schedule under two hours.
3. The personal sources follow the phase: fetched when signed in, after a
   check when checking, quietly probed on Android after an upgrade from v0.3.

Then: back in the foreground, only stale sources are fetched (personal over
5 minutes, menus over 30 or a new day, the schedule over 2 hours); pull to
refresh fetches everything, or re-runs the silent check under the "session
ended" banner; a sign-in or a check that came back fetches the personal ones.
Two sources noticing one expired session (a failed fetch, and the desktop's
WebView transport) collapse into one check. The coordinator also owns the
15-second clock tick that keeps every countdown honest, and exposes `busy`
(the thin bar under the header), `offline` and the header's "updated …".

The requests themselves got fewer. The chapel's three JSON endpoints run at
once (`std::async`), and the student ID they need is remembered, so the 59 KB
dashboard is read once per sign-in rather than once per refresh. The meal
plan's target is remembered the same way: one request instead of two. A
remembered ID that stops working is re-read once. `-v` logs each source's
request count and time.

### Loading state

Each source has a `SourceStatus` (`chapel.skipsStatus`, `chapel.scheduleStatus`,
`dining.menuStatus`, `dining.planStatus`): `hasData`, `loading`, `fromCache`,
`stale`, `updatedText`, `error`, `needsSignIn`. Every screen reads it the same
way: a skeleton until there is data; the data, stamped with its age when it is
a saved copy or stale; an inline message with Retry (or Sign in) when there is
nothing and nothing is coming. A failed refresh never blanks figures already
shown.

## The on-device cache

`PayloadCache` keeps, per source, the **raw payload** the service sent —
`<stateDir>/cache/<key>.json`, 0600, written atomically — not the parsed
models. Reading it back runs the providers' own parsers again, so there is no
serialiser to keep in step, and a parser fix applies to data saved before it.
`chapel` and `meals` are personal and deleted on sign-out; `menus` and
`schedule` are public and kept. Sample-data preview neither reads nor writes
it, nor `SessionState`.

`SessionStore` writes are read-modify-write (`update()`): the login controller
and the chapel viewmodel used to save whole copies of the state, and chapel's
copy, read at startup, wrote a stale `lastLogin` of 0 back over a fresh
sign-in.

---

## The screens

Four tabs, behind a bottom bar, and search over them:

| Tab | Viewmodel | What it shows |
|---|---|---|
| **Today** | `today` (over `chapel`, `dining`, `curfew`) | a hero chosen by the clock — the chapel on a chapel morning, the next sitting through the day, curfew from 8 PM or once The Commons has closed — then the rest of the day; at night, what is still open and tomorrow morning |
| **Chapel** | `chapel` | skips left as pips, the schedule by week (series marked "Part 1 of 2"), the ledger, why attendance is required; a sheet per chapel with Watch live |
| **Dining** | `dining`, `hours` | Plan (meals, flex, the weekly flex pace, exchanges, activity), Menu (a week of days, the sittings, Avoid chips, three stations, the all-day ones) and Hours (open now, what opens next, the day as a timeline, swipe periods) |
| **Campus** | `campus`, `curfew` | tonight's curfew, then buildings grouped by when they close, with search, filters and starred places |
| search | `search` | places, dishes and speakers, from what is already loaded |

Today is deliberately *not* owned by one source. It reads the skip ledger and
the meal plan (behind the Cedarville sign-in), the chapel schedule and the
menus (public), and the hand-entered hours and curfew. The public half fills in
before you sign in and stays after you sign out, so it is never entirely blank.

**The hours are hand-entered**, like the term dates and curfew: The Commons'
sittings, the dining venues and their meal-exchange windows, the swipe periods
and all nineteen buildings live in `core/hours.cpp`, copied from The Commons'
dining page and Campus Security's building-hours page. (They lived in the QML
until v0.4.) A window past midnight runs past 1440 — 12:45 AM is 1485 — and two
cells of the buildings page are read as documented in `hours.h`. Everything
there is a pure function of the clock it is handed, so Today, Hours, Campus and
search agree.

The viewmodels are exposed as context properties (`bridge`, `login`, `sync`,
`chapel`, `dining`, `curfew`, `hours`, `campus`, `today`, `search`,
`settings`), which QML resolves by name at runtime. A typo is therefore not a
compile error; `tests/tst_qml_contract.cpp` reads every `chapel.<name>`-style
reference out of the QML and checks it against the classes' `staticMetaObject`,
every `chapel.skipsStatus.<field>` against `SourceStatus`, every `model.<role>`
and every delegate's `required property` against the list models' role names.

Two rules the screen is built on, both of which have bitten this codebase:

* **No confident zeros.** A figure the server did not report renders as an
  em dash. `$0.00` and `0 skips left` are the two most alarming things this app
  could say and it must never say either by accident — which is why the core
  uses `std::optional` and invalid dates for "not reported", and the viewmodels
  translate those to `-1` and `""` for QML, which has no null. (A figure that
  has simply not loaded yet is a skeleton, not a dash: see Loading state.)
* **No glyphs from fonts.** Every icon is drawn on a `Canvas` in `Glyph.qml`,
  from SVG path data on a 24×24 grid (the same strings as the design canvas).
  The toolbar once used "↻" (U+21BB), which the desktop font has and the
  phone's Roboto does not, so it rendered as a tofu box on the moto g power. An
  icon that fails to render in a tab bar leaves the user with no idea what the
  tab is. Canvas is part of QtQuick proper — no icon font, no QtSvg.

  The logo is the one exception: it is `qml/icon.png`, a raster PNG, which
  satisfies the same rule for the same reason — nothing about it depends on
  what the device has installed. The two text typefaces are bundled for the
  same reason (`qml/fonts/`, registered by `main.cpp` before QML loads), as
  static Latin instances made from Google Fonts' variable files.

The QML is one module, `CedarView`, compiled ahead of time by qmlcachegen and
embedded in the binary (`qt_add_qml_module` in `CMakeLists.txt`). A new QML file
goes in that list. Each platform's build gets only its own web surface —
`WebSurfaceDesktop.qml` imports QtWebEngine, which does not exist on Android,
and the deployment tool decides what to bundle by reading these files.

Shadows are drawn by `RectangularShadow` *behind* a card, never by an effect
layer *around* it: a renderer without shader effects (software, or the
screenshot run) then loses the shadow and still draws the card.

`cedarview --shoot <dir>` (desktop only) saves every screen — Welcome, Today
morning and night, Chapel and its sheet, Plan, Menu, Hours, Campus, Search — in
both themes at 360 and 412 px wide, plus a cold start's skeletons, from sample
data at a pinned clock (`--clock`, default Thu Sep 17 2026, 9:42 AM).

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
