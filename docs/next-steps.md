# Next steps

What is done, what is not, and exactly what only you can do.

**Revised 2026-09-23, after the move from Python to C++.** The app is Qt 6.11
C++ and QML now; the Python app, its APK toolchain and its pytest suite are in
git history only.

---

## What actually works right now

Verified by running it, not by reading it:

- **The test suite passes** — 14 Qt Test suites under ctest, offscreen, no
  network: parsing, transport, expiry detection, session persistence, the
  login state machine, the viewmodels, the QML contract, and the in-page fetch
  against a real (desktop) browser.
- **The app runs.** `./build/cedarview --demo` loads the QML and populates all
  four sources from fixtures, and every tab renders pixel-identical to the
  Python app's `--demo`:

  ```
  chapel: 3 ledger entries, used=2 of 18, remaining=16
  chapel schedule: 20 chapels, next is SGA
  meal plan: 16 meals, dining=$102.34, flex=, 19 transactions
  dining: 2 days loaded
  ```

- **The live desktop app reaches the public services** (menus, chapel schedule)
  over HTTPS and opens Microsoft's sign-in on an empty session.
- **All five data points are done against real sources**, not guesses — see
  `docs/data-sources.md`.
- **The Android build passes every gate** — signed, 16 KB-aligned, the right
  manifest, `INTERNET` only, 21 MB to download. Full detail in
  `docs/android-status.md`.

## What has never been executed

All of it needs you:

- A completed interactive sign-in **on the desktop app**, and the checks that
  follow it (§2).
- The C++ build **on the phone** (§1). The previous, Python build of the same
  design did run there; this one has only been checked off the device.
- The first **real** `--aab`, signed with `~/keys/cedarview-release.jks`, and
  the Play Console setup (`docs/iterating-and-shipping.md` §3.3).

---

## 1. On the phone

```bash
nix develop --command cedarview-android-build -c './scripts/build-apk --install'
adb logcat -c && adb logcat | grep -iE 'mycu|qml|cedarview'
```

Then work through the checklist in `docs/android-status.md` §3.2: sign-in,
Chucks and Chapel filling in (they come through the new JNI HTTPS path),
edge-to-edge, status-bar icons, the back gesture, the theme toggle.

## 2. Sign in on the desktop app — ~5 minutes

```bash
nix develop
cmake -B build -G Ninja && cmake --build build
./build/cedarview
```

A window opens, sends you to Microsoft if the session is not alive, and then
shows your real figures. Your existing desktop sign-in should carry over from
the Python app: same state directory (`~/.local/share/mycu`), same web profile.
Worth doing by hand once:

- relaunch → the session persisted, no login prompt
- Sign out → login surface reappears
- let it sit until the session expires → it re-authenticates on the next refresh

## 3. A known rough edge, inherited

On a cold start with no session, the surface reports the Self-Service URL the
moment it is told to go there — before the redirect to Microsoft has happened —
so `LoginController` briefly counts that as signed in and the first chapel and
meal-plan fetches fail (the browser refuses them mid-redirect). The sign-in
surface then opens as it should, and signing in reloads everything. The Python
app did exactly the same; the port kept it rather than change behaviour in
passing. The fix is probably to take "signed in" from a *finished* load on
Self-Service rather than from the URL alone. `scripts/check-live` works around
it by waiting and retrying.

## 4. More providers

Grades, schedule, student account. One provider each:

1. capture the page with `scripts/discover`, 2. scrubbed fixture,
3. `src/core/providers/<name>.{h,cpp}`, 4. `tests/tst_<name>_provider.cpp`,
5. viewmodel + QML page — and add it to that screen's `refreshAll()`.

Steps 1–4 need no phone, no display and no network. The session and transport
are untouched.

## 5. Housekeeping

- **The Python build's leftovers** in the project (`.venv-android/`,
  `.android-wheels/`, `android-build/`) are no longer used by anything and can
  be deleted. `~/.buildozer` (the SDK) and `~/.pyside6_android_deploy` (NDK
  r27c) are still used by `scripts/build-apk` — keep those.
- **`scripts/discover`'s network recorder** has not been installing since the
  desktop profile moved to a persistent one: it looks for a `_default_profile`
  the backend no longer has, and says "no network recorder" at startup. The
  port kept that as it was. `--devtools` still gives the full Network panel.
- **A smaller APK**, if it ever matters: Quick Controls ships every style's
  plugin (Material, Fusion, Universal, Imagine, FluentWinUI3) and the QML
  debugging plugins, and only one style is used.

---

## Judgement calls, and why

**Both parser branches while the payload was unknown.** Writing the JSON and
HTML paths and sniffing at runtime meant the app would work either way on the
first authenticated run. Discovery settled it — the chapel data is JSON — and
the HTML branch, the tolerant key matching and the invented `AttendanceStatus`
vocabulary were all deleted. Tolerant matching is for surviving an unknown, not
for keeping after it is known.

**A port, not a redesign.** The C++ keeps the Python module for module: the
same seams, names, sentinels, error copy and QML. Improvements the port made
visible (the cold-start rough edge above, the recorder) are written down rather
than folded in, so the move itself changes nothing a user can see.
