# Android: status, how the build works, and what is left

**Written 2026-09-17, at the point where production was paused.**

This is the handoff document. It covers what was done, how Android packaging
works *for this project specifically*, exactly where we are, and what to do
next. `docs/android.md` is the reference manual for the toolchain; this file is
the narrative and the to-do list.

---

## 1. The headline

**The app builds, installs, launches and runs on the phone.** A moto g power 5G
(2024) — arm64-v8a, Android 15, SDK 35 — is showing live Cedarville data
fetched over HTTPS from a natively-packaged PySide6/QML application.

Verified on-device, from logcat:

```
INFO mycu: myCU 0.1.0 — android / python 3.11.13 / linux
INFO mycu.platform.android: QtWebView initialised
INFO mycu.core.transport: ssl: loaded 145 system CAs from /apex/com.android.conscrypt/cacerts
INFO mycu.ui.transport_webview: transport attached to QObject
INFO mycu.ui.viewmodels.chapel: chapel schedule: next is Worship Chapel
INFO mycu.ui.viewmodels.dining: dining: 7 days loaded
```

Screenshots confirmed the Chapel and Dining tabs rendering real data, and the
embedded WebView reaching Microsoft's sign-in page (`aadcdn.msauth.net` in the
Chromium console log).

What is **not** yet proven: a completed interactive sign-in on the phone, and
therefore the authenticated chapel-skip fetch. See §6.

---

## 2. How Android development works in this project

Worth reading once, because it is not like a normal Android build and almost
nothing about it is obvious.

### The shape of it

This is a **Python** app. There is no Java/Kotlin source and no Android Studio
project. What ships in the APK is:

1. a **cross-compiled CPython 3.11.13** built for `arm64-v8a`,
2. **Qt 6.11 for Android** and the PySide6 bindings, taken from prebuilt
   wheels Qt publishes,
3. **your `mycu/` package and `main.py`**, copied in as ordinary `.py` files,
4. a small Java bootstrap (`org.kivy.android.PythonActivity`) that starts the
   interpreter.

Four tools are stacked, and every one of them can fail on its own terms:

```
scripts/build-apk            ← our driver; encodes every workaround
  └── pyside6-android-deploy ← Qt's tool: picks Qt modules, writes buildozer.spec
      └── buildozer          ← fetches the SDK/NDK, drives the next layer
          └── python-for-android (p4a)  ← cross-compiles CPython + recipes
              └── Gradle / Android SDK  ← actually assembles the APK
```

When something breaks, the error usually surfaces several layers above the
cause. The recurring pattern in this project was **a silent failure low down
that only becomes visible much later** — see §4.

### Why NixOS makes it harder

`pyside6-android-deploy` is not in nixpkgs. It ships in the PyPI wheels and
drives buildozer/p4a, which download their own SDK and NDK as generic
dynamically-linked Linux binaries and assume an FHS filesystem. NixOS has no
`/usr/lib`, so `flake.nix` provides a project-local `buildFHSEnv`
(`mycu-android-build`) rather than enabling `programs.nix-ld` globally.

Everything Android must run **inside that shell**:

```bash
nix develop --command mycu-android-build -c './scripts/build-apk'
```

### Two Pythons, deliberately

| | Version | Job |
|---|---|---|
| Desktop devShell | 3.13.15 | runs the app and the test suite on this machine |
| FHS Android shell | **3.11** | runs the packaging tool |
| On the phone | **3.11.13** | runs the app |

They must not be conflated. `pyside6-android-deploy` refuses to start on
anything newer than 3.11, and Qt only publishes `cp311` Android wheels.

### The build is done from a *copy* of the project

`scripts/build-apk` does not build from your working tree. It copies the
project into `android-build/` and builds there, minus two desktop-only files.
Reasons, in full, are in the header of that script; the short version is that
`pyside6-android-deploy` decides what to bundle by **scanning every `.py` and
`.qml` file**, and two files mention `QtWebEngine`, which does not exist on
Android. Your working tree is never modified, so an interrupted build cannot
leave the repo half-changed.

### The build in phases

`scripts/build-apk` runs:

1. **Preconditions** — FHS shell, JDK, Python 3.11, adb/device.
2. **Toolchain** — creates `.venv-android`, installs `pyside6==6.11.0` plus
   `requirements-android.txt`.
3. **Wheels** — downloads the Qt-for-Android wheels into `.android-wheels/`.
4. **SDK licences** — accepts them non-interactively.
5. **Staging** — copies the project to `android-build/`, drops the two
   desktop-only files, and fails if a `QtWebEngine` *import* remains.
6. **Phase 1** — `pyside6-android-deploy --init`, which writes
   `pysidedeploy.spec` and `buildozer.spec` and then exits.
7. **Phase 1a** — rewrites `buildozer.spec`: permissions pinned to `INTERNET`,
   `p4a.commit` pinned to a CPython 3.11 revision.
8. **Phase 1b** — installs an `aclocal` shim (see §4).
9. **Phase 2** — `buildozer android debug` does the real work.
10. **Verification** — checks the built APK's bundled `libpython` against
    shiboken's `DT_NEEDED`, and its manifest permissions. **Fails the build**
    on either mismatch.

The split into two phases exists because steps 7 and 8 must happen *between*
config generation and the build, and the tool normally does both in one go.

---

## 3. What was changed in the repo

All 16 files, with the reason:

| File | Change |
|---|---|
| `main.py` *(new)* | Android entry point. p4a's bootstrap imports a top-level `main.py` by name. Calls `mycu.ui.app.main([])` with empty argv and logs startup tracebacks to stdout. |
| `mycu/core/minihtml.py` *(new)* | ~80-line stdlib HTML tree replacing `lxml`. |
| `mycu/core/providers/meals.py` | Uses `minihtml` instead of `lxml`. |
| `mycu/core/transport.py` | `ssl_context()` — loads the device's CA store on Android. Thread-safe. |
| `mycu/ui/qml/Main.qml` | Refresh button text (tofu fix); Dialog binding-loop fix. |
| `mycu/ui/qml/WebSurfaceAndroid.qml` | `# VERIFY:` comment resolved — enum spelling confirmed. |
| `pyproject.toml` | `lxml` dropped from `dependencies`. |
| `flake.nix` | Python 3.11, `android-tools`, and the host libraries the toolchain needs. |
| `scripts/build-apk` | Rewritten as a working driver. |
| `.gitignore` | Build artifacts. |
| `tests/test_meals_provider.py` | +2 tests (unclosed `<p>`, `<script>` exclusion). |
| `tests/test_core_is_qt_free.py` | +1 test: no C extensions in the runtime path. |
| `tests/test_transport.py` | +6 tests for the TLS trust store. |
| `README.md`, `docs/android.md`, `docs/next-steps.md` | Status and corrections. |
| `pysidedeploy.spec` *(new, generated)* | Record of what went into the APK. |

**Tests: 215 → 225, all passing.** `/etc/nixos` was **not** touched.

### The lxml decision

`docs/android.md` said lxml would disappear once discovery showed the chapel
page was JSON. That was wrong: the chapel page *is* JSON, but the **meal-plan
page is server-rendered HTML** and had inherited the dependency. lxml is a C
extension, and every C extension needs a python-for-android recipe.

Rather than cross-compile libxml2/libxslt, `meals.py` moved to
`mycu/core/minihtml.py` — stdlib `html.parser` implementing the four DOM
operations that file actually uses. The runtime path is now pure Python, and
`test_core_is_qt_free.py` blocks `lxml`/`bs4`/`numpy`/etc. at import and
re-parses the real fixture, so a reintroduced C extension fails in the test
suite rather than 40 minutes into an APK build.

---

## 4. The seven things that broke, and why they matter

Each cost real time and each is now encoded in `flake.nix` or
`scripts/build-apk`. **If you change the toolchain, re-read this list** — these
are the failure modes to expect.

1. **adb needed no system change.** `docs/android.md` said to set
   `programs.adb.enable`, add yourself to `adbusers`, rebuild and log out.
   Unnecessary — logind already grants an ACL on the phone's USB node
   (`getfacl` shows `user:kroma:rw-`). `android-tools` went into `flake.nix`
   instead. This mattered: `/etc/nixos` has unrelated uncommitted edits
   (google-chrome, flameshot, stegseek, a `codex-cli-nix` input) that a
   `nixos-rebuild switch` would have activated as a side effect.

2. **Python 3.13 → 3.11.** The tool refuses newer; Qt ships only `cp311`.

3. **Missing host libraries.** Qt's tools inside the wheel need `libzstd`,
   `libgssapi_krb5`, `libbrotlidec`. A missing one appears only as
   `exit status 127` from a subprocess.

4. **ncurses headers.** nixpkgs splits headers into a `dev` output.
   `libncurses.so` was present, `curses.h` was not, so CPython's configure
   enabled `_curses` from the library alone and `make` **aborted** (Makefile
   target, not an optional setup.py module) fifteen minutes in.

5. **Autotools too new.** p4a builds libffi 3.4.2, whose `configure.ac` calls
   the legacy `AC_PROG_LIBTOOL`; autoconf 2.73 no longer expands libtool's
   `AU_ALIAS` for it. Pinned `autoconf269`, added `m4`.

6. **p4a discards `ACLOCAL_PATH`.** It rebuilds each recipe's environment from
   scratch, copying only `PATH` and proxy vars. On NixOS that is fatal:
   aclocal's built-in macro directory is automake's own `/nix/store` prefix,
   which has no libtool macros, so the FHS env's `ACLOCAL_PATH` is load-bearing.
   Without it aclocal **succeeds** while silently omitting `libtool.m4`.
   `scripts/build-apk` shims `aclocal`/`libtoolize` onto `PATH`, which p4a does
   preserve.

7. **The staging directory must not start with a dot.** It was
   `.android-build`. buildozer's `_copy_application_sources()` filters hidden
   directories with `if True in [x.startswith('.') for x in root.split(sep)]`,
   testing the **absolute** path. One dotted component anywhere above the
   project makes every file look hidden, so buildozer copied **nothing**,
   reported nothing, and the build died forty minutes later with
   `BUILD FAILURE: No main.py(c) found in your app directory`.

### And two that only the device could find

8. **`libshiboken6.abi3.so` hard-links `libpython3.11.so`.** Everything in the
   wheels is named `*.abi3.so`, which suggests the stable ABI makes the CPython
   version irrelevant. It does not — shiboken carries a real `DT_NEEDED`. p4a's
   `develop` branch builds CPython **3.14**, so the APK built, signed,
   installed, launched, logged `Qt platform plugin started`, and then died:

   ```
   dlopen failed: library "libpython3.11.so" not found
   java.lang.UnsatisfiedLinkError ... qtMainLoopThread
   ```

   Fixed by pinning `p4a.commit = 3762c88c…` (2025-10-26, the commit before
   p4a moved to 3.14). **`scripts/build-apk` now fails the build** if the
   bundled `libpython` and shiboken's `DT_NEEDED` disagree.

9. **No TLS trust store on Android.** A genuine app bug the desktop build could
   never expose: Android has no OpenSSL default cert path, so every
   `HttpTransport` request failed with `CERTIFICATE_VERIFY_FAILED`, which reads
   like a network fault. `ssl_context()` now detects a context that loaded zero
   CAs and falls back to the device's own store.

   Note the first attempt at this was **wrong**: it used
   `load_verify_locations(capath=...)`, which OpenSSL treats as a *lazy*
   lookup — nothing loads, `cert_store_stats()` keeps reporting zero, and the
   tests still passed because on a desktop the default context already has CAs
   and **the fallback branch never ran**. The fix reads the PEM blocks and
   passes `cadata`, which is verifiable; the test now builds a fake Android CA
   directory and drives the fallback directly.

---

## 5. Exactly where we are

### Working, confirmed on hardware

- APK builds reproducibly via one command.
- Installs and launches; process stays alive.
- CPython 3.11.13, Qt 6.11, QtWebView all initialise.
- QML renders; Chapel and Dining tabs both work.
- **Dining**: 7 days of live Home Cooking menus with allergen tags.
- **Next chapel**: live ("Worship Chapel, Tomorrow 10:00 AM").
- HTTPS verification works via the device CA store.
- The embedded WebView reaches Microsoft's sign-in page.
- Manifest requests **`INTERNET` and nothing else** — the packaging tool wanted
  to add `ACCESS_FINE_LOCATION`, `WRITE_EXTERNAL_STORAGE` and
  `ACCESS_NETWORK_STATE`; the build strips them and re-verifies with `aapt`.

### State of the artifacts, right now

**Corrected 2026-09-17 19:5x — the claim below turned out to be false.**

The 18:17 `mycu.apk` was described here as "the newest build, includes the two
QML fixes". It did not. Unzipping it and reading
`assets/private.tar → mycu/ui/qml/Main.qml` shows `text: "↻"` and the
un-fixed Dialog: **that APK was built from source predating both fixes.** The
fixes existed only in the working tree and had never been in an APK, which is
why the tofu box survived a clean install onto the phone.

Two lessons, both cheap:

- **A timestamp is not evidence of contents.** `unzip -p mycu.apk` and read the
  file; the QML ships as plain text inside `private.tar`.
- **`adb install -r` is not enough to prove what is running.** p4a reuses the
  extracted `_python_bundle`, so the first install showed *neither* the old nor
  the new behaviour reliably. `adb uninstall && adb install` re-extracts, and
  logcat prints `extracting _python_bundle/...` when it really happens.

To pick up the newer build:

```bash
nix shell nixpkgs#android-tools --command adb install -r mycu.apk
```

If the UI looks unchanged, uninstall first — p4a reuses the already-extracted
app data (see the caveat in §6).

### Found on the device, 2026-09-17: sign-in was *impossible* on Android

Worth reading in full, because "nobody has typed credentials yet" turned out to
be hiding a real bug rather than waiting on a human.

Driving the app on the phone showed it sitting on the Chapel tab with a red
`Couldn't reach Self-Service: TypeError: Failed to fetch` banner, the footer
stuck on "Connecting to Cedarville…", and **no sign-in surface**. Logcat had
the answer:

```
I chromium: [INFO:CONSOLE:0] "Access to fetch at
'https://selfservice.cedarville.edu/cedarinfo/chapelskip' from origin
'https://login.microsoftonline.com' has been blocked by CORS policy"
source: https://login.microsoftonline.com/81c32413-…/saml2?SAMLRequest=…
```

So the WebView **had** reached Microsoft's sign-in page — the CORS refusal
proves which origin it was on — but Python never learned it. `LoginController`
logs every status change, and logcat stops at "Connecting to Cedarville…".

The cause: **`WebView.url` on QtWebView is the URL that was *requested*, not
the one the browser ended up on.** A server-side redirect — exactly how the
SAML flow begins — never updates it. `currentUrl` was bound to that property,
so `looks_like_login()` never saw `login.microsoftonline.com` and the surface
was never shown. The app could display public data forever and never
authenticate.

This is the same class of difference as the missing cookie API: QtWebView is
thinner than WebEngineView, and the places it is thinner are not documented
together anywhere. `loadRequest.url`, from `onLoadingChanged`, is the only
carrier of the post-redirect URL.

Fixed in two independent places, because one of them is a QML signal and QML
signals are exactly what cannot be tested headlessly:

1. `WebSurfaceAndroid.qml` sets `currentUrl` from `loadRequest.url`.
   `tests/test_platform_selection.py` asserts the handler does so and that
   `currentUrl` is never re-bound to `view.url`.
2. `WebViewTransport.get()` treats a *refused* request (`TypeError: Failed to
   fetch` and friends — no status, no body) as a probable expired session: it
   asks the page for `window.location.href`, which is always the truth, and
   raises `SessionExpired` if that is a sign-in page. So the login surface
   opens even if the URL signal is missed again.

The second one also turns a confusing error into the correct remedy: the user
now gets a sign-in page instead of "Couldn't reach Self-Service".

### Not yet proven

- **A completed sign-in on the phone.** The surface now opens (fix above), but
  nobody has typed credentials and come back.
- The authenticated chapel-skip fetch on Android.
- Whether federated logout drops the Self-Service cookie (`# VERIFY:` in
  `platform/android.py`).
- Whether a large body survives `runJavaScript` on QtWebView — proven at 300 KB
  on QtWebEngine, undocumented on the system WebView.

---

## 6. Next steps, in order

### 1. Install the current build and sign in (10 minutes, only you can do it)

**Use a clean install, not `-r`** — see the artifacts note in §5. The cookie jar
is empty anyway until a sign-in succeeds, so nothing is lost:

```bash
nix shell nixpkgs#android-tools --command adb uninstall com.kromakobra.cedarview
nix shell nixpkgs#android-tools --command adb install mycu.apk
nix shell nixpkgs#android-tools --command adb logcat -c
# launch it, sign in on the phone, then:
nix shell nixpkgs#android-tools --command adb logcat -d | grep -E 'python  :'
```

This settles the largest remaining unknown. Expect either chapel skips and meal
balances to appear, or a parse error naming exactly what differs.

What "working" looks like in logcat, now that the redirect fix is in: a
`login: Sign in with your Cedarville account` line, Microsoft's page on screen,
and after you authenticate, `login: ` (blank) followed by `chapel: N ledger
entries` and `meal plan: N meals`. If you get the CORS `TypeError: Failed to
fetch` **with no sign-in page**, the redirect fix has regressed — that exact
symptom is the bug documented in §5.

**Caveat worth knowing:** p4a extracts `_python_bundle` into app-private
storage on first run and **reuses it**. After changing the bundled CPython this
caused `failed to get the Python codec of the filesystem encoding`. A plain
`adb install -r` does not always re-extract. If behaviour looks stale:

```bash
adb uninstall com.kromakobra.cedarview && adb install mycu.apk
```

That wipes the WebView cookie jar, so you sign in again.

### 2. Settle the remaining `# VERIFY:` items

Two of the original three were resolved **offline**, by reading the Android
wheel rather than using the phone — worth remembering as a technique, since
anything about Qt's *API shape* is answerable that way:

- `WebSurfaceAndroid.qml`'s enum spelling is **correct** — `plugins.qmltypes`
  confirms `LoadStatus = {LoadStartedStatus, LoadStoppedStatus,
  LoadSucceededStatus, LoadFailedStatus}`.
- The **optional JNI upgrade is ruled out**: `QJniObject`, `QJniEnvironment`
  and `QAndroidApplication` are all absent from the Android `QtCore.abi3.so`.
  The WebView transport is not a stopgap; on PySide6 6.11 it is the only
  option.

The two that still need the device: federated logout, and large-body
`runJavaScript`.

### 3. Shrink the APK

148 MB is large for an app with two screens. The bundle currently includes
QtQuick3D, QtCharts, QtSensors and QtTest QML plugins pulled in by the wheel.
`excluded_qml_plugins` in `pysidedeploy.spec` already lists them, but the Qt
libs themselves are still copied. Worth an hour with `--qt-libs` once the app
is functionally complete. Not urgent.

### 4. A release keystore, if you ever want upgrades to survive

Currently a debug-signed APK, which is correct for a personal phone. The debug
key is per-machine; if it is regenerated, the next install fails with
`INSTALL_FAILED_UPDATE_INCOMPATIBLE` and needs an uninstall (wiping the
session). See "Signing" in `docs/android.md`.

### 5. Housekeeping

- `main.py`, `mycu/core/minihtml.py` and `pysidedeploy.spec` are **untracked** —
  commit them with the rest.
- Build artifacts occupy **~4.5 GB**, and `git clean` will not reclaim most of
  it, because buildozer ignores `BUILDOZER_HOME`:

  | Path | Size | Reclaim |
  |---|---|---|
  | `android-build/` | 548 MB | `./scripts/build-apk --clean` |
  | `.venv-android/` | 708 MB | delete; rebuilt automatically |
  | `.android-wheels/` | 80 MB | delete; re-downloaded |
  | `~/.buildozer/` | 511 MB | Android SDK — keep |
  | `~/.pyside6_android_deploy/` | 2.6 GB | Android NDK — keep |

---

## 7. If you change one thing, change it here

- **Qt/PySide6 version** → `PYSIDE_VERSION` in `scripts/build-apk` *and* the
  devShell in `flake.nix`. They must match. Then check whether Qt still ships
  `cp311` wheels; if it moves to a newer CPython, update `P4A_COMMIT` too.
- **Target ABI** → `ARCH` in `scripts/build-apk` (`x86_64` for an emulator).
- **Permissions** → phase 1a in `scripts/build-apk`. Do not edit
  `buildozer.spec` by hand; it is regenerated on every build.
- **Anything in `mycu/platform/desktop.py` or `WebSurfaceDesktop.qml`** → those
  are the two files the staging step removes. If either is renamed, update the
  prune list, or the guard will fail the build (by design).
