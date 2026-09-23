# Testing

```bash
nix develop
cmake -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure     # 14 suites, ~5s, no network, no display
./build/cedarview --demo                       # the whole app against fixtures
python scripts/smoke-transport                 # the dev scripts' transport, real QtWebEngine
python scripts/check-live                      # the real chapel requests, once
```

## The two guarantees

**No test can reach the network.** Every suite starts through
`CEDARVIEW_TEST_MAIN` (`tests/testsupport.h`), which points Qt's application
proxy at a port nothing listens on, so any Qt Network request fails at once
instead of quietly reaching `selfservice.cedarville.edu`. No test constructs an
`HttpTransport` either. A test that hit the network would pass on your laptop,
fail everywhere else, and — worse — make the suite's result depend on whether
you happen to be logged in. `scripts/check-live` is a script precisely so it
does not have to be a test.

The one exception is `tst_webview_transport`, which runs a real browser against
a loopback HTTP server on 127.0.0.1 and nothing else. It leaves the proxy alone
because Chromium honours it too, and would then refuse even the loopback.

**Qt never needs a display.** `QT_QPA_PLATFORM=offscreen` is set before the
application object exists (and again by ctest), so the Qt-touching suites run
over SSH and in a build sandbox. The same helper points `MYCU_STATE_DIR` at a
throwaway directory, so no test can touch your real session.

## What each suite covers

| Suite | Covers |
|---|---|
| `tst_transport` | URL resolution, expiry detection both ways (host, not substring), `Response`, `FixtureTransport` and its slugs, routing, telling a CORS refusal from a network fault |
| `tst_session` | Persistence, 0600/0700 permissions, corrupt and old-schema files, reading a file the Python app wrote, the state-dir rules |
| `tst_calendar` | Term boundaries, days left, the bar, the between-terms answer |
| `tst_chapel_provider` | The real capture: reported figures used verbatim, the ledger order, the student-ID bootstrap, the four requests, fines failing softly, a login page raising `SessionExpired` |
| `tst_chapel_schedule_provider` | The real capture, UTC to local, speakerless chapels, paging, "now" and "over" |
| `tst_dining_provider` | The real capture, slot ordering, allergens, the serving hours and the next sitting |
| `tst_meals_provider` | The real page and endpoint, which balance is which, plan cycles, activity, the attribute finder |
| `tst_core_is_gui_free` | The architectural rule: `cedarview_core` links Qt Core only, and no GUI include hides in the sources |
| `tst_tasks` | Results and typed exceptions crossing threads; a destroyed caller not being called back |
| `tst_login_flow` | The whole auth state machine, driven by URLs alone |
| `tst_viewmodels` | List-model roles, error translation, the `-1` sentinel, re-entrancy, the schedule, paging the Chucks tab, activity |
| `tst_qml_contract` | Every `chapel.x` / `dining.x` / … and every `model.role` in the QML exists in C++ |
| `tst_surfaces` | The web surfaces' shared interface, their imports, and that each platform builds only its own |
| `tst_webview_transport` | The in-page fetch end to end: offscreen QtWebEngine, the real surface QML, a loopback server |

## The tests that exist because something went wrong

Worth keeping, and worth understanding before you "simplify" the code they
guard:

- `anIdpInAQueryParameterIsNotAnExpiredSession` — a tracking pixel's URL
  carried `ref=https://login.microsoftonline.com/`, and a substring match
  declared the session expired. Expiry keys on the host.
- `theLedgerMustNotBeUsedToComputeSkipsUsed` — the ledger sums to 1, the
  server says 2, and the server is right; recomputing shows a wrong number.
- `orderingKeysOnSlotNotOnTheFreeTextMealLabel` — "yogurt bar", a breakfast
  block, sorted after dinner when the sort keyed on `meal`.
- `anUnnamedChapelDoesNotPrintItsOwnNameTwice` — "Worship Chapel" above
  "Worship Chapel", seen on the phone.
- `signOutOnlyCompletesOnceWeAreBackOnSelfservice` — the logout URL contains
  `post_logout_redirect_uri`, so matching on it declared victory the instant
  we navigated.
- `theAndroidSurfaceTakesItsUrlFromTheLoadRequest` — QtWebView's `url` does
  not follow redirects, so sign-in could never start on the phone.
- `theRefreshGestureReachesEverySourceOnTheScreen` — pull-to-refresh called
  `refresh()`, which on the Dining screen reloads the menu and not the
  balances.
- `aDestroyedContextIsNotCalledBack` — a background result delivered to an
  object that had gone away while the work ran.

## Manual checks that need a session

1. `./build/cedarview` → sign in → the Summary fills in.
2. Relaunch → no login prompt (the profile persisted).
3. Overflow menu → Sign out → the login surface reappears.
4. Leave it until Entra expires the session, then refresh → it re-authenticates
   by itself and the data arrives.

## On Android

```bash
adb logcat -c && adb logcat | grep -iE 'mycu|qml|cedarview'
```

The checklist of things only a device can answer is at the end of
`docs/android.md`.
