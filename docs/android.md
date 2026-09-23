# Android: toolchain and packaging

**Status: the toolchain has been run.** The build was executed against a
moto g power 5G (2024) — arm64-v8a, Android 15, SDK 35 — on 2026-09-17.
Everything in "The build" below is a transcript of what actually happened, not
a plan. The on-device behaviour of the app is a separate question and is still
tracked in the verification table at the bottom.

Everything is driven by **`scripts/build-apk`**, which encodes all of it:

```bash
nix develop --command mycu-android-build -c './scripts/build-apk --install'
```

Read the header comment of that script before changing it — it documents two
bugs in `pyside6-android-deploy` 6.11 that dictate the shape of the build.

---

## The build

### 1. adb needs no system configuration

An earlier version of this document told you to set `programs.adb.enable` and
add yourself to `adbusers` in `/etc/nixos/configuration.nix`, then log out and
back in. **That is not necessary and you should not bother.**

systemd-logind already grants the active seat's user an ACL on the plugged-in
phone's USB node:

```console
$ getfacl /dev/bus/usb/003/004
user::rw-
user:kroma:rw-        <- already there, no group membership involved
```

So `adb` works as soon as it is on `PATH`, which `flake.nix` now arranges via
`android-tools` in both the devShell and the FHS env. No rebuild, no logout.

This matters beyond convenience: `/etc/nixos` usually has unrelated pending
edits in it, and `nixos-rebuild switch` would activate all of them at once just
to get a phone talking to a laptop.

What *is* needed is on the phone: Settings → About → tap Build Number seven
times → Developer options → USB debugging. Then plug in and accept the RSA
prompt. Until you accept it the device shows as `unauthorized`:

```console
$ adb devices -l
ZD222VD73W    unauthorized usb:3-4        <- unlock the phone and accept
ZD222VD73W    device       usb:3-4 ...    <- ready
```

### 2. The FHS shell

`pyside6-android-deploy` is **not** in nixpkgs — confirmed, there are no
`pyside6-*` tools on `PATH` in the devShell. It ships in the PyPI `pyside6`
wheels and drives buildozer / python-for-android underneath, which download
their own SDK and NDK and expect an FHS layout.

`programs.nix-ld` is not enabled on this system, and rather than enable it
globally, `flake.nix` provides a project-local `buildFHSEnv`. That keeps the
mess inside this one project:

```bash
nix develop            # normal devShell
mycu-android-build     # drops into the FHS shell
# or, from anywhere:
nix run .#android-shell
```

`scripts/build-apk` creates `.venv-android` inside it and installs everything.
Three things about that venv are load-bearing:

- **Python 3.11, not 3.13.** `pyside6-android-deploy` refuses to start on
  anything newer ("Android deployment requires Python version 3.11 or lower.
  This is due to a restriction in buildozer."), and Qt only publishes Android
  wheels tagged `cp311`. `flake.nix` therefore puts `python311` in the FHS env
  while the desktop devShell stays on 3.13. The two never meet.
- **`requirements-android.txt`.** The deploy tool needs `jinja2`, `pkginfo`,
  `tqdm` and `packaging==24.1`, which are *not* dependencies of the `pyside6`
  wheel. Without them it exits with a bare "The following packages are
  required but not installed".
- **The host Qt tools need system libraries.** `qmlimportscanner` and friends
  ship inside the manylinux wheel and expect an FHS system to provide
  `libzstd`, `libgssapi_krb5` and `libbrotlidec`. A missing one surfaces only
  as `exit status 127` from a subprocess, which is deeply unhelpful; run the
  binary by hand to see the real error. These are in `flake.nix` now.
- **Autotools must be *old*, not current.** python-for-android builds libffi
  v3.4.2, whose `configure.ac` still calls the legacy `AC_PROG_LIBTOOL`.
  libtool supplies that only as `AU_ALIAS([AC_PROG_LIBTOOL], [LT_INIT])`, and
  **autoconf 2.73 no longer expands the alias** — the token survives into the
  generated `configure`, trips autoconf's `^AC_` pattern check, and
  `autoreconf -vif` fails:

  ```
  configure.ac:41: error: undefined or overquoted macro: AC_PROG_LIBTOOL
  configure.ac:418: warning: AC_PROG_LD is m4_require'd but not m4_defun'd
  ```

  `flake.nix` therefore pins **`autoconf269`**. `m4` is also required
  explicitly — `libtoolize` shells out to it and otherwise stops with
  "Please install GNU M4", which is not obviously an autotools-version
  problem at all.
- **…and p4a throws `ACLOCAL_PATH` away.** Even with the right autoconf, the
  libffi recipe still failed with `possibly undefined macro: AC_PROG_LIBTOOL`,
  while running `autoreconf -vif` by hand in the same directory worked. The
  difference is that `pythonforandroid/archs.py` builds each recipe's
  environment from scratch, copying only `PATH` and the HTTP proxy variables.

  This is specifically a NixOS problem: aclocal's default macro directory is
  compiled in as automake's own prefix, a `/nix/store` path containing no
  libtool macros. The FHS env exposes them at `/usr/share/aclocal` and points
  `ACLOCAL_PATH` there — so when p4a drops the variable, aclocal silently
  omits `libtool.m4` from `aclocal.m4` and `AC_PROG_LIBTOOL` survives
  unexpanded into `configure`. **Silently** is the operative word: aclocal
  succeeds, and the failure only appears two steps later.

  `scripts/build-apk` writes shim `aclocal`/`libtoolize` scripts that
  re-export `ACLOCAL_PATH` and puts them early on `PATH`, which p4a *does*
  preserve. To confirm the diagnosis rather than trusting it, run `autoreconf
  -vif` in the libffi build directory under `env -u ACLOCAL_PATH` with and
  without the shim on `PATH`: exit 1 versus exit 0.
- **Headers, not just libraries.** nixpkgs splits headers into a package's
  `dev` output, and `targetPkgs` only pulls in the default one. That bit hard
  with ncurses: `/usr/lib/libncurses.so` existed but `/usr/include/curses.h`
  did not, so the host CPython's `configure` detected curses *from the library
  alone*, enabled `_curses` and `_curses_panel`, and then died at compile time
  with a wall of `unknown type name 'WINDOW'`. Those are Makefile targets, not
  optional `setup.py` modules, so **`make` aborts** instead of skipping them
  and the whole build fails about fifteen minutes in. `ncurses.dev` is in
  `targetPkgs` now. If another host-build failure looks like this, check for
  the matching `.dev` output before anything else.

`BUILDOZER_HOME` is set to `.buildozer-home/` in the project, but be aware
**buildozer 1.5.0 ignores it** for its global directory: the SDK and NDK land
in `~/.buildozer` and `~/.pyside6_android_deploy` regardless, several GB of
them. `git clean` will not reclaim those.

### 3. The Android wheels

These are the Qt-for-Android builds and they are **not on PyPI** — only on
`download.qt.io`. `scripts/build-apk` fetches them into `.android-wheels/`:

```
https://download.qt.io/official_releases/QtForPython/pyside6/pyside6-6.11.0-6.11.0-cp311-cp311-android_aarch64.whl
https://download.qt.io/official_releases/QtForPython/shiboken6/shiboken6-6.11.0-6.11.0-cp311-cp311-android_aarch64.whl
```

`aarch64` is right for this phone (`adb shell getprop ro.product.cpu.abi` →
`arm64-v8a`). The `x86_64` variants exist for an emulator.

**The `cp311` in those filenames is load-bearing. The device's CPython must be
3.11.** This is the single most expensive thing to get wrong here, because it
does not fail at build time.

The trap is that the wheels look version-independent. Every Python extension
module in them is named `*.abi3.so` — `QtCore.abi3.so`, `libpyside6.abi3.so`,
`libshiboken6.abi3.so` — and stable-ABI modules *are* forward-compatible, so
it is easy to conclude that a wheel built for 3.11 will load on anything
newer. It will not: **`libshiboken6.abi3.so` carries a hard `DT_NEEDED` on
`libpython3.11.so`** regardless of its abi3 name. The APK then builds, signs,
installs and launches; Qt gets as far as logging `Qt platform plugin started`;
and then:

```
dlopen failed: library "libpython3.11.so" not found:
  needed by .../lib/arm64/libshiboken6.abi3.so
java.lang.UnsatisfiedLinkError ... qtMainLoopThread
```

Check the link dependency, not the filename:

```bash
python3 -c "
import re, zipfile
b = zipfile.ZipFile('.android-wheels/shiboken6-6.11.0-6.11.0-cp311-cp311-android_aarch64.whl'
     ).read('shiboken6/libshiboken6.abi3.so')
print(sorted(set(re.findall(rb'libpython[0-9.]+\.so', b))))   # -> [b'libpython3.11.so']
"
```

`pyside6-android-deploy` writes `p4a.branch = develop`, and python-for-android
moved its `python3` recipe to **CPython 3.14** in commit `e1bd249`
(2025-10-28). `scripts/build-apk` therefore also pins

```
p4a.commit = 3762c88c56e3443efb8eba2a02a2604b680240fd   # 2025-10-26, python3 3.11.13
```

— the commit immediately before that bump, so the Qt bootstrap stays current
while CPython stays 3.11.13. The script additionally compares shiboken's
`DT_NEEDED` against the `libpython*.so` actually inside the finished APK and
**fails the build** if they disagree, so this cannot silently reach the phone
again. Revisit the pin when Qt ships Android wheels for a newer CPython.

### 4. The Android SDK licence

buildozer downloads the SDK on first run and then stops on Google's licence
prompt, because its stdin is not a terminal. The failure is reported several
minutes later as a bare non-zero exit from `buildozer android debug` with no
mention of licences. `scripts/build-apk` accepts them up front via
`sdkmanager --licenses`; acceptance is recorded in
`~/.buildozer/android/platform/android-sdk/licenses/` and persists.

---

## Packaging this app

### The entry point must be `main.py`

python-for-android's bootstrap imports a top-level module called exactly
`main.py`. `main.py` in the repo root is that shim; it calls
`mycu.ui.app.main([])` with an empty argv, because `sys.argv` on Android is
whatever the bootstrap left behind and feeding it to `argparse` risks a
`SystemExit(2)` that looks, on a phone, like the app simply failing to open.

### QtWebEngine, and why the build uses a staging copy

`pyside6-android-deploy` decides what to bundle by **scanning every `.py` and
`.qml` file under the project directory**. Two files here are desktop-only and
name QtWebEngine in an import:

```
mycu/platform/desktop.py           from PySide6.QtWebEngineQuick import …
mycu/ui/qml/WebSurfaceDesktop.qml  import QtWebEngine
```

QtWebEngine does not exist on Android, so if the scanner sees either, the build
dies with `FileNotFoundError: libQt6WebEngineCore_arm64-v8a.so not found inside
the wheel`.

**Pinning `modules` in `pysidedeploy.spec` does not fix this**, which is worth
knowing because it looks like it should. Two bugs in the 6.11 tool:

1. `deploy_lib/config.py` ends `Config.__init__` with `self.modules = []`. That
   is the property *setter*, which writes an empty `modules` back into the
   parsed config before `AndroidConfig` reads it. A pinned value is always
   discarded and the scan always runs.
2. `deploy_lib/android/android_config.py` reads `ndk_path` under
   `elif not existing_config_file:` — inverted. Supplying a spec that already
   contains `ndk_path` makes it read `None` and crash with
   `TypeError: unsupported operand type(s) for /: 'NoneType' and 'str'`.

So `scripts/build-apk` builds from `android-build/`, a copy of the project
with those two files removed, and lets the tool generate its own spec there.
Your working tree is never modified, so an interrupted build cannot leave the
repo half-changed. A guard in the script fails the build if a QtWebEngine
*import* reappears in the staging tree — matched on import syntax, not the bare
word, since the prose in this codebase mentions QtWebEngine constantly.

**The staging directory must not start with a dot.** It was `.android-build`
at first, and the build failed at the very last step with:

```
BUILD FAILURE: No main.py(c) found in your app directory.
```

buildozer's `_copy_application_sources()` skips hidden directories with

```python
if True in [x.startswith('.') for x in root.split(sep)]: continue
```

which tests the **absolute** path rather than the path relative to
`source.dir`. One dotted component anywhere above the project makes every file
look hidden, so buildozer copies nothing, reports nothing, and only p4a's
entry-point check notices — about forty minutes of recipe building later. The
same trap applies to keeping the whole project under a dotted directory.

Nothing is lost on device: `mycu.platform.current_backend()` imports the
desktop backend by name and only when not on Android.

### Modules actually bundled

Auto-discovered from the staging tree, and recorded in the generated
`pysidedeploy.spec`:

```
modules = Core,Gui,Network,OpenGL,Qml,Quick,QuickControls2,WebView
android plugins = platforms_qtforandroid,webview_qtwebview_android
excluded_qml_plugins = QtCharts,QtQuick3D,QtSensors,QtTest,QtWebEngine
```

**`WebView` is the one that matters** — without it there is no login and no
transport. The symptom is `ImportError: PySide6.QtWebView` in logcat, which
`mycu/platform/android.py` turns into an explicit message pointing here.

### C extensions: there are none, keep it that way

Every C extension in the runtime path needs a python-for-android
cross-compilation recipe, which is the difference between a packaging exercise
and a porting project.

`lxml` used to be one. The chapel page turned out to be JSON, but the
**meal-plan page is server-rendered HTML**, so the dependency did not simply
disappear with M0 — it moved to `meals.py`. It was retired instead by
replacing that one parser with `mycu/core/minihtml.py`, ~80 lines of
`html.parser` implementing the four DOM operations `meals.py` needs.

`pyproject.toml`'s `dependencies` is therefore pure-Python, and
`tests/test_core_is_qt_free.py::test_core_imports_with_no_c_extension_dependencies_available`
blocks `lxml` and friends at import time and re-parses the real fixture, so a
reintroduced C extension fails in the test suite rather than 40 minutes into an
APK build.

### Permissions

`INTERNET` only. The app reads your own records from your own account; it needs
nothing else. Do not let the packaging tool add more by default — check the
generated `AndroidManifest.xml`.

### Signing, and the three build modes

`scripts/build-apk` has three outputs:

| Flag | Output | Signed with | For |
|---|---|---|---|
| (none) | `mycu.apk` | the per-machine debug key | your own phone over adb |
| `--release` | `cedarview-<version>-arm64-v8a.apk` | your release key | GitHub Releases (sideload) |
| `--aab` | `cedarview-<version>.aab` + `.apks` | your release (upload) key | Google Play |

`--release` and `--aab` both need the four `P4A_RELEASE_*` variables; the
runbooks are §3.6 (GitHub) and §3.3 (Play) of `docs/iterating-and-shipping.md`.
Either one fails the build if the artifact comes out unsigned.

The debug key is generated per machine (`~/.android/debug.keystore`). If it is
ever regenerated, the next install is treated as a *different app* and
`adb install -r` fails with `INSTALL_FAILED_UPDATE_INCOMPATIBLE`; uninstall
first. That wipes the WebView cookie jar, so you sign in again.

`.gitignore` excludes `*.keystore`, `*.jks`, `*.pass` and `keystore.properties`.
**Back the release keystore up somewhere outside this repo.** It is not
recoverable. Losing it means you can never ship an upgrade to an
already-installed sideloaded build, and on Play it means requesting an
upload-key reset.

### Google Play's requirements

Enforced by the build and re-checked on every artifact: target API 36, minSdk
28, every native library 16 KB-aligned, and an App Bundle. The details, and
the two workarounds they needed (python-for-android's link flags and a rebuilt
`libshiboken6`), are in §3.2 of `docs/iterating-and-shipping.md`.

---

## `adb install -r` does not update your Python or QML

**Read this before debugging a UI change that "did not take".** It is the most
misleading failure mode in the whole loop, because everything reports success.

The APK does not run `mycu/` out of the APK. python-for-android ships the app
as `assets/private.tar` and the Java bootstrap untars it into

```
/data/data/com.kromakobra.cedarview/files/app/
```

on **first** launch, then writes a stamp at `files/app/private.version` and
skips the unpack on every launch after that. `adb install -r` keeps the data
directory. So a reinstall gives you new native libraries and the *old* `.py`
and `.qml` files, and the app runs happily against them.

Seen 2026-09-17, after the UI overhaul: the build was correct — `tar tf` on
`assets/private.tar` listed all twelve new QML files — the install said
`Success`, and the phone went on rendering the previous screen. The only clue
was a QML warning naming a line number that no longer had that code on it.

Check what the device actually has, rather than what you built:

```bash
adb shell run-as com.kromakobra.cedarview ls files/app/mycu/ui/qml/
tar tf <(unzip -p mycu.apk assets/private.tar)   # what you shipped
```

Force the unpack:

```bash
adb shell pm clear com.kromakobra.cedarview
```

That wipes the app's data directory, which is also where the session lives —
so it signs you out. There is no way to re-extract without doing so; the stamp
is the only mechanism, and bumping `version` in `buildozer.spec` would make a
new stamp but is not worth doing per UI tweak.

**`./scripts/build-apk --install` only works if the flag reaches the script.**
Inside the FHS shell, `-c` takes a single command *string* — anything after it
becomes `$0`, not an argument:

```bash
# wrong: --install lands in $0 and is silently ignored
nix develop --command mycu-android-build -c ./scripts/build-apk --install

# right
nix develop --command mycu-android-build -c './scripts/build-apk --install'
```

The script does not fail when this happens. It prints the manual `adb install`
line at the end instead, which reads like normal output.

---

## Debugging on-device

```bash
adb logcat -c                     # clear first, or you drown
adb logcat | grep -iE 'python|qml|mycu'
```

`mycu/ui/app.py` logs to **stdout** deliberately — python-for-android tags
stdout with the app name, which makes it greppable.

### Things flagged for on-device verification

The first three are marked `# VERIFY:` in the source; the last three came with the move to targetSdk 36:

| Where | What to check |
|---|---|
| ~~`qml/WebSurfaceAndroid.qml`~~ | **Resolved statically — no device needed.** `plugins.qmltypes` inside the `android_aarch64` wheel gives `LoadStatus = {LoadStartedStatus, LoadStoppedStatus, LoadSucceededStatus, LoadFailedStatus}`, `loadingChanged(QQuickWebViewLoadRequest)`, and `url`/`status`/`errorString` on the request. The QML is correct as written. |
| `platform/android.py` | Whether the federated logout round trip really drops the Self-Service cookie, not just the Entra one. |
| transport, end to end | That a large body survives `runJavaScript` on QtWebView. It was verified at 300 KB on QtWebEngine by `scripts/smoke-transport`; the marshalling limits on the system WebView are not documented. If it truncates, the fix is to chunk the body in the read script. |
| `qml/Main.qml`, edge to edge | At targetSdk 36 the ribbon must sit below the status bar with its colour behind it, and the tab bar above the gesture handle. Verified on desktop with simulated insets only. |
| `ui/app.py`, status-bar icons | That light and dark themes each get contrasting status-bar icons (driven by `styleHints().setColorScheme`). |
| back gesture | That back still behaves as before at targetSdk 36, via `enableOnBackInvokedCallback="false"` (patched into the manifest by `scripts/build-apk` phase 1g). |

The wheel is worth interrogating before reaching for the phone — anything about
Qt's *API shape* is answerable offline:

```bash
python3 -c "
import zipfile
z = zipfile.ZipFile('.android-wheels/pyside6-6.11.0-6.11.0-cp311-cp311-android_aarch64.whl')
print(z.read('PySide6/Qt/qml/QtWebView/plugins.qmltypes').decode())
"
```

### The optional JNI upgrade — ruled out

The idea was: if `QJniObject` exists in the Android PySide6 build, then
`android.webkit.CookieManager.getInstance().getCookie(url)` yields the real
cookies, and the WebView transport could be swapped for a plain `httpx` client.

**It does not exist.** Checked against the shipped Android binding rather than
inferred from the desktop build:

```console
$ python3 -c "
import zipfile
d = zipfile.ZipFile('.android-wheels/pyside6-6.11.0-6.11.0-cp311-cp311-android_aarch64.whl').read('PySide6/QtCore.abi3.so')
print([s for s in (b'QJniObject', b'QJniEnvironment', b'QAndroidApplication') if s in d])
"
[]
```

No JNI symbols at all in the Android `QtCore`. So the WebView transport is not
a stopgap pending a better approach — on PySide6 6.11 it is *the* approach, and
`docs/architecture.md`'s reasoning stands unchanged. Revisit only if a future
PySide6 exposes the JNI classes.

Strictly an optimisation. Only attempt it once the APK ships.
