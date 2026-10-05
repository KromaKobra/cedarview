# Testing

```bash
nix develop
cmake -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure     # 18 suites, ~5s, no network, no display
./build/cedarview --demo                       # the whole app on sample data
./build/cedarview --shoot shots/               # every screen, both themes, two widths, as PNGs
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
| `tst_session` | Persistence, 0600/0700 permissions, corrupt and unknown-schema files, a v1 file keeping its sign-in, read-modify-write, reading a file the Python app wrote, the state-dir rules |
| `tst_cache` | The on-device cache: round trips through the real parsers, 0600, sign-out deleting only the personal keys, corrupt and other-schema files |
| `tst_calendar` | Term boundaries, days left, the between-terms answer |
| `tst_curfew` | The curfew rule, late nights past midnight |
| `tst_chapel_provider` | The real capture: reported figures used verbatim, the ledger order, the student-ID bootstrap, a known ID skipping the dashboard, a stale ID re-read once, the three requests running at once, fines failing softly, a login page raising `SessionExpired` |
| `tst_chapel_schedule_provider` | The real capture, UTC to local, speakerless chapels, paging, "now" and "over" |
| `tst_dining_provider` | The real capture, slot ordering, allergens, the NEW marker and stray asterisks, the serving hours and the next sitting |
| `tst_meals_provider` | The real page and endpoint, which balance is which, plan cycles, activity, a remembered target, the attribute finder |
| `tst_core_is_gui_free` | The architectural rule: `cedarview_core` links Qt Core only, and no GUI include hides in the sources |
| `tst_tasks` | Results and typed exceptions crossing threads; partial results arriving in order; a destroyed caller not being called back |
| `tst_login_flow` | The phases: Welcome, the silent check and its grace, needs-sign-in, signing in from each place, the address asked for not counting as a sign-in, a refused renewal asking instead of looping, preview, sign-out |
| `tst_viewmodels` | List-model roles, error translation, the `-1` sentinel, re-entrancy, opening on saved data, the meal plan's errors and expiry, sign-out, the schedule and its series, the Menu section's stations, Avoid and day strip, the flex pace |
| `tst_hours` | The hours tables, past-midnight windows, the Hours section's hero and timeline, the Campus tab's groups, filters, search and stars, curfew's evening bar |
| `tst_sync` | The coordinator, with real worker threads: what is fetched at launch on each platform and phase, the Android probe only after an upgrade from v0.3, fresh saved data not refetched, two expiries making one check, a refused renewal ending in the banner, offline, sign-out and staying signed out, preview never touching the user's data |
| `tst_qml_contract` | Every `chapel.x` / `dining.x` / … and every `chapel.skipsStatus.x`, `model.role` and delegate `required property` in the QML exists in C++; every refresh goes through `sync` |
| `tst_surfaces` | The web surfaces' shared interface (including `loading`/`loadProgress`/`pageLoaded`), their imports, and that each platform builds only its own |
| `tst_bottom_sheet` | `BottomSheet.qml` run for real: it rises flush with the bottom edge, every time it is opened |
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
- `theAddressAskedForIsNotASignIn` — the surfaces report the address they
  are sent to before the redirect to Microsoft, and any Self-Service address
  used to count as signed in: on the phone every sign-in "finished" at once
  and failed as an expired session.
- `aSignInFromTheBannerIgnoresTheMicrosoftPageItWasLeftOn` — Android's surface,
  shown, first reports the page it was already on; from the "session ended"
  banner that is Microsoft's, which is why "reached Microsoft" comes from a
  page that loaded.
- `aRenewalRefusedAtOnceAsksInsteadOfCheckingAgain` and
  `aRenewalTheFetchesRefuseEndsInTheBanner` — a check that says yes and a
  fetch that says no would otherwise take turns for as long as the app was
  open.
- `anAndroidLaunchAfterSigningOutLooksForNoSession` — signed out stays signed
  out: no quiet fetch at the next launch.
- `theSheetRisesAgainAfterClosing` — the closing transition animated `y` and
  replaced its binding, so every sheet opened only once; after that the scrim
  dimmed the screen over nothing.
- `theAndroidSurfaceTakesItsUrlFromTheLoadRequest` — QtWebView's `url` does
  not follow redirects, so sign-in could never start on the phone.
- `everyRefreshGoesThroughTheCoordinator` — pull-to-refresh called
  `dining.refresh()`, which reloads the menu and not the balances.
- `eachWriterChangesOnlyItsOwnField` — the chapel viewmodel saved its stale
  copy of the session over the login controller's fresh sign-in.
- `aDestroyedContextIsNotCalledBack` — a background result delivered to an
  object that had gone away while the work ran.

## Screenshots

`./build/cedarview --shoot <dir>` puts every screen up in turn and saves it:
`<theme>-<width>-<screen>.png`, for both themes at 360 and 412 px, plus
`*-cold-today.png`, taken while the sample data is still held back, to check
the skeletons. It runs on sample data with the clock pinned (`--clock`,
default 2026-09-17T09:42), so the shots are the same every time. Compare them
with the design canvas. Under `QT_QPA_PLATFORM=offscreen` Qt Quick may fall
back to software rendering, which draws no shadows; everything else is as on
a phone.

## Manual checks that need a session

1. `./build/cedarview` on a fresh state dir → Welcome; Sign in → Microsoft's
   page inside CedarView's bar → the figures fill in from skeletons.
2. Relaunch → the figures are there at once, stamped "Updated …"; no
   Microsoft page flashes by.
3. Disconnect → Retry → "You're offline", with the saved figures kept.
4. Leave it until Entra expires the session, then refresh → it renews by itself,
   or the "Your session ended" banner appears over the saved figures.
5. More → Sign out → Welcome at once, with no Microsoft page; relaunch →
   Welcome, and no personal figures; Sign in → Microsoft asks for the account
   and password, and the figures fill in.
6. Welcome → "Look around with sample data" → every tab filled; Sign in from
   the banner.

## On Android

```bash
adb logcat -c && adb logcat | grep -iE 'mycu|qml|cedarview'
```

The checklist of things only a device can answer is at the end of
`docs/android.md`.
