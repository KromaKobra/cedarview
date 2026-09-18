# Next steps

What is done, what is not, and exactly what only you can do.

**Revised 2026-09-17 (evening).** The previous version of this file was written
before discovery and is kept only in git history — it described the chapel page
as an unknown and asked you to run `scripts/check-live` to settle it. That is
done: the page is JSON, the endpoints are known, and the parser is written
against a real capture. Read this version, not that one.

---

## What actually works right now

Verified by running it, not by reading it:

- **279 tests pass** (`pytest`) — parsing, transport protocol, expiry detection,
  session persistence, the login state machine, viewmodels, the QML contract.
  No test can open a socket; `conftest.py` blocks it.
- **The app runs.** `python -m mycu --demo` loads the QML and populates all four
  sources from fixtures:

  ```
  chapel: 3 ledger entries, used=2 of 18, remaining=16
  chapel schedule: next is Worship Chapel
  meal plan: 19 meals, dining=112.08, flex=0.0
  dining: 2 days loaded
  ```

- **All five data points are done against real sources**, not guesses — see
  `docs/data-sources.md`. Chapel skips (three JSON endpoints), meals left and
  both dollar balances (server-rendered page), Home Cooking menus and the next
  chapel speaker (two public APIs, no login).
- **The WebView transport works end to end.** `scripts/smoke-transport` drives a
  real QtWebEngine surface against a loopback server: 7/7, including a 300 KB
  body surviving `runJavaScript`, two concurrent requests not being confused,
  and a stalled request raising instead of hanging.
- **The environment is confirmed.** Against your actual pin (`nixos-26.05`):
  PySide6 6.11.0, shiboken6 6.11.0, Qt 6.11.1, qtwebengine 6.11.1.
- **The Android app runs on hardware.** Built, installed and launched on a
  moto g power 5G (2024) — arm64-v8a, Android 15. QtWebView initialises, the
  QML loads, and Dining and next-chapel show live data over HTTPS.
  `scripts/build-apk` does the whole thing. Full detail in
  `docs/android-status.md`.

## What has never been executed

Two things, and **both need you** — they are the entire remaining list:

- A completed interactive sign-in **on the phone**, and therefore the
  authenticated chapel-skip and meal-plan fetches on Android. The WebView is
  confirmed to reach Microsoft's page, and the bug that stopped the app from
  ever *showing* it to you is fixed (§1) — but nobody has typed credentials and
  come back.
- A completed sign-in **on the desktop app** (`python -m mycu`, as opposed to
  `--demo`). The authenticated endpoints themselves are proven — `scripts/discover`
  signed in and captured all of them — but the assembled app has not been driven
  through a live session start to finish.

---

## 1. Sign in on the phone — blocking, ~10 minutes, only you can do it

**This is the one thing standing between the repo and a finished app.**

Note that this step was previously blocked by a real bug, not by waiting on a
human: **interactive sign-in was impossible on Android.** The WebView reached
Microsoft's page but Python never found out, so the sign-in surface never
opened. Diagnosis and fix in `docs/android-status.md` §5 — the short version is
that `WebView.url` on QtWebView reports the URL *requested*, not the one landed
on, so a redirect is invisible. Fixed, and the rebuilt `mycu.apk` contains it.

**Clean-install rather than `-r`**: p4a reuses the extracted `_python_bundle`,
and a plain `-r` can leave you running the old code while looking at a new APK.

```bash
nix shell nixpkgs#android-tools --command adb uninstall org.mycu.mycu
nix shell nixpkgs#android-tools --command adb install mycu.apk
nix shell nixpkgs#android-tools --command adb logcat -c
# launch it, sign in on the phone, then:
nix shell nixpkgs#android-tools --command adb logcat -d | grep -E 'python  :'
```

Expect either the chapel skip count and meal balances to appear, or an error
naming exactly what differs. Either outcome is progress; paste the logcat lines
if it is the second.

The uninstall wipes the WebView cookie jar, so you sign in again — which is the
point of this step anyway.

## 2. Sign in on the desktop app — ~5 minutes

```bash
nix develop
python -m mycu
```

A window opens, sends you to Microsoft if the session is not alive, and then
shows your real figures. Worth doing by hand once:

- relaunch → the session persisted, no login prompt
- Sign out → login surface reappears
- let it sit until the session expires → it re-authenticates on the next refresh

## 3. The two remaining `# VERIFY:` items

Both need the device; neither blocks use of the app.

| Where | What |
|---|---|
| `platform/android.py` | Whether the federated logout actually drops the Self-Service cookie, not just the Entra one. |
| the transport | That a large body survives `runJavaScript` on QtWebView. Proven at 300 KB on QtWebEngine; the system WebView's marshalling limits are undocumented. If it truncates, chunk the read script. |

`python scripts/smoke-transport --android` on-device settles both at once.

The third item on the original list — the `WebView.LoadFailedStatus` enum
spelling — was **resolved offline** by reading `plugins.qmltypes` in the Android
wheel. Worth remembering as a technique: anything about Qt's *API shape* is
answerable that way, without the phone.

## 4. Fixed on 2026-09-17, from driving it on the device

Three bugs, all found by running the app on the phone and reading logcat rather
than by reading the source. Two of them were invisible to the test suite by
construction, which is the point worth keeping.

**Sign-in was impossible on Android.** The big one; see §1 and
`docs/android-status.md` §5.

**An unnamed chapel printed its own name twice** — "Worship Chapel" above
"Worship Chapel". `UpcomingChapel.is_same_as_title` asked "does the title
repeat the *speakers*?", which is trivially False when there are no speakers —
and that is exactly the case where `who` has already fallen back to the title.
It now compares against `who`, which is what the UI renders.

Note the existing test **asserted the bug** (`is_same_as_title is False` for
precisely the chapel that rendered wrong). A test can only protect the
invariant it states, and that one stated the implementation rather than the
requirement.

### Refresh only covered half of each screen

Recorded here because it is the kind of bug that hides in plain sight.

Both screens draw on two unrelated sources — Chapel shows the authenticated
skip ledger plus the public upcoming-chapel feed; Dining shows the public menu
API plus the authenticated meal-plan balances. The toolbar Refresh button and
both pull-to-refresh handlers called `refresh()`, which fetches **only the first
of the two**. So the meals-left and dollar figures loaded once at sign-in and
then never moved again for the rest of the run, no matter how hard you pulled —
on a number that goes down every time you eat.

The fix is `refreshAll()` on each viewmodel, composing the sources that belong
to that screen, with the QML calling that instead. The composition lives in the
viewmodel so a third provider is wired in one place rather than in every gesture
handler.

`tests/test_qml_contract.py` is new and guards the general case: the viewmodels
reach QML as **context properties**, the loosest binding Qt offers, so
`chapel.refrehAll()` is not an error anywhere — it is a silent no-op on desktop
and one line in logcat on the phone. The test asserts that every `chapel.<name>`
and `dining.<name>` in the QML resolves to a real `Property` or `Slot` on the
metaobject, and that no user-facing refresh gesture calls the half-measure
`refresh()` again.

## 5. More providers

Grades, schedule, student account. One module each:

1. capture the page with `scripts/discover`, 2. scrubbed fixture,
3. `providers/<name>.py`, 4. `tests/test_<name>_provider.py`, 5. viewmodel +
QML page — and add it to that screen's `refreshAll()`.

Steps 1–4 need no phone, no display and no network. The session and transport
are untouched.

## 6. Housekeeping, when it matters

- **Shrink the APK.** 148 MB for two screens; QtQuick3D, QtCharts, QtSensors and
  QtTest come along from the wheel. Worth an hour with `--qt-libs` once the app
  is functionally complete. Not urgent.
- **A release keystore**, if you ever want upgrades to survive a regenerated
  debug key. See "Signing" in `docs/android.md`.
- **Build artifacts occupy ~4.5 GB.** The table in `docs/android-status.md` §6.5
  says what is safe to delete.

---

## Two judgement calls made early, and why

**`mycu/platform/` instead of a top-level `platform/`.** The original plan's
tree put it at the root, which would shadow the standard library's `platform`
module for anything on `sys.path` — and Qt, setuptools and python-for-android
all import it. The failure surfaces far from its cause and is miserable to debug
inside a buildozer run. Nesting it under `mycu` keeps `import platform`
resolving to the stdlib; `tests/test_core_is_qt_free.py` asserts the nesting
stays.

**Both parser branches while the payload was unknown.** Writing the JSON and
HTML paths and sniffing at runtime meant the app would work either way on the
first authenticated run. Discovery settled it — the chapel data is JSON — and
the HTML branch, the tolerant key matching and the invented `AttendanceStatus`
vocabulary are all deleted. Tolerant matching is for surviving an unknown, not
for keeping after it is known.
