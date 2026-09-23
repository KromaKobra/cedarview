# Android: toolchain and packaging

Everything is driven by **`scripts/build-apk`**, which encodes all of it:

```bash
nix develop --command cedarview-android-build -c './scripts/build-apk --install'
```

The app is C++ and QML. The APK holds Qt's own libraries and one of ours,
`libcedarview_arm64-v8a.so` — no Python, no interpreter, nothing unpacked on
first launch. The build is Qt's standard one: `qt-cmake` from the Qt Android
kit configures `CMakeLists.txt`, and its `apk`/`aab` target runs
`androiddeployqt` and Gradle.

---

## The build

### 1. adb needs no system configuration

systemd-logind already grants the active seat's user an ACL on the plugged-in
phone's USB node:

```console
$ getfacl /dev/bus/usb/003/004
user::rw-
user:kroma:rw-        <- already there, no group membership involved
```

So `adb` works as soon as it is on `PATH`, which `flake.nix` arranges via
`android-tools` in both the devShell and the FHS env. There is no need for
`programs.adb.enable` or the `adbusers` group, and no reason to run
`nixos-rebuild` — which would also activate whatever else is pending in
`/etc/nixos`.

What *is* needed is on the phone: Settings → About → tap Build Number seven
times → Developer options → USB debugging. Then plug in and accept the RSA
prompt. Until you accept it the device shows as `unauthorized`:

```console
$ adb devices -l
ZD222VD73W    unauthorized usb:3-4        <- unlock the phone and accept
ZD222VD73W    device       usb:3-4 ...    <- ready
```

### 2. The FHS shell

Everything the Android build runs is a prebuilt Linux binary that expects an
FHS layout: the NDK's clang, the SDK tools, Gradle's JVM, and the host Qt tools
(`moc`, `rcc`, `qmlcachegen`, `androiddeployqt`) from the Qt kits. Rather than
enable `programs.nix-ld` globally, `flake.nix` provides a project-local
`buildFHSEnv`:

```bash
nix develop                 # normal devShell
cedarview-android-build     # drops into the FHS shell
# or, from anywhere:
nix run .#android-shell
```

The FHS env carries the host tools and the shared libraries the host Qt tools
need (`libzstd`, `libgssapi_krb5`, `libbrotlidec`, the X/GL libraries). A
missing one surfaces only as `exit status 127` from a subprocess; run the
binary by hand to see which `.so` it wants, e.g.
`.qt/6.11.1/gcc_64/libexec/qmlimportscanner --help`.

**`scripts/build-apk` re-runs itself under `env -i`.** The FHS shell is entered
from the devShell and inherits all of it — the desktop Qt's directories on
`PATH`, `NIXPKGS_CMAKE_PREFIX_PATH`, `CMAKE_*_PATH`, `NIX_CFLAGS_COMPILE`.
CMake's package search walks `PATH`, so inside the Android toolchain
`find_package(Qt6)` finds the *desktop* Qt and dies in
`Qt6CoreConfigExtras.cmake` with a wall of `include()` errors. Starting from an
empty environment and adding back only what the build needs is the fix that
does not break the next time nixpkgs adds a variable.

**`-c` takes a single command string.** Inside the FHS shell anything after
`-c`'s argument becomes `$0`, not an argument:

```bash
# wrong: --install lands in $0 and is silently ignored
nix develop --command cedarview-android-build -c ./scripts/build-apk --install

# right
nix develop --command cedarview-android-build -c './scripts/build-apk --install'
```

### 3. The Qt kits

Two kits of **Qt 6.11.1** — the same release as the desktop Qt in `flake.lock`,
so the QML tested on the desktop is the QML that ships — fetched with
[aqtinstall](https://github.com/miurahr/aqtinstall) into the gitignored `.qt/`
on the first run (about 2 GB):

```
.qt/6.11.1/android_arm64_v8a/   the Android kit, with QtWebView
.qt/6.11.1/gcc_64/              the matching host Qt, whose tools the build runs
.qt/aqt-venv/                   aqtinstall itself — a host tool, never in the APK
```

From Qt 6.8 on, the Android kits are published under aqt's `all_os` host, not
`linux`; `aqt list-qt linux android` stops at 6.7 and looks like 6.11 does not
exist.

### 4. SDK and NDK

The ones the Python build downloaded are reused as they are:

```
~/.buildozer/android/platform/android-sdk                       platforms;android-36, build-tools 37
~/.pyside6_android_deploy/android-ndk/android-ndk-r27c           NDK 27.2.12479018
```

Override with `ANDROID_SDK_ROOT` / `ANDROID_NDK_ROOT`. The script checks the
NDK is **r27c**, because that is what Qt 6.11.1's Android binaries were built
with (`lib/cmake/Qt6/qt.toolchain.cmake` in the kit records
`android-ndk-r27c`). It needs no newer NDK for 16 KB pages: r27 supports them
with `ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON`, which the script passes.

Gradle and the Android Gradle Plugin (9.0, from Qt's template) are downloaded
by Gradle itself on the first build and cached under `~/.gradle`.

---

## Packaging this app

### What is in the APK

Qt Core, Gui, Qml, Quick, Quick Controls and their QML plugins, QtWebView, and
`libcedarview`. `androiddeployqt` works this out from the link line and from
scanning the QML, which is why each platform's build gets only its own web
surface (`CMakeLists.txt`): `WebSurfaceDesktop.qml` imports QtWebEngine, which
does not exist on Android.

**QtWebView is the one that matters** — without it there is no login and no
transport. It is linked (`Qt6::WebView`) and initialised in
`src/platform/android.cpp`.

Our library is stripped at link time. The NDK compiles with `-g` in every
configuration and leaves stripping to Gradle, which cannot find the NDK's
`strip` when the NDK lives outside the SDK; it says "Unable to strip the
following libraries" and ships them whole. Qt's own libraries arrive stripped.

### HTTPS without a bundled OpenSSL

The public services (menus, chapel schedule) are fetched directly rather than
through the browser. On Android that goes through
`android/src/com/kromakobra/cedarview/HttpGet.java` — `HttpsURLConnection`,
called over JNI from `src/core/httptransport.cpp`. Qt has no native TLS backend
on Android; Qt Network there would need OpenSSL and a CA bundle inside the
APK. The platform's own stack uses the phone's own trust store instead. (This
replaces the Python build's `ssl_context()`, which went looking for Android's
CA directory by hand.)

### The manifest

`android/AndroidManifest.xml` is Qt 6.11's template, customised; its header
comment lists what changed and why. `CMakeLists.txt` assembles it, the
adaptive-icon XML and the Java into `build-android/android-package/`, copying
the launcher icon PNGs in from `assets/` so they have a single source.

### Permissions

`INTERNET` only. The app reads your own records from your own account; it
needs nothing else. The manifest declares it explicitly instead of taking Qt's
`%%INSERT_PERMISSIONS` placeholder, and strips with `tools:node="remove"` what
libraries would merge back in — including androidx.core's
`DYNAMIC_RECEIVER_NOT_EXPORTED_PERMISSION`, which nothing here uses.
`scripts/build-apk` fails the build if the result is ever more than `INTERNET`.

### Signing, and the three build modes

| Flag | Output | Signed with | For |
|---|---|---|---|
| (none) | `build-android/cedarview-<version>-arm64-v8a-debug.apk` | `~/.android/debug.keystore` | your own phone over adb |
| `--release` | `cedarview-<version>-arm64-v8a.apk` | your release key | GitHub Releases (sideload) |
| `--aab` | `cedarview-<version>.aab` + `.apks` | your release (upload) key | Google Play |

`--release` and `--aab` need four variables — `QT_ANDROID_KEYSTORE_PATH`,
`_ALIAS`, `_STORE_PASS`, `_KEY_PASS` — or the `P4A_RELEASE_*` names this
project used before, which are accepted as aliases. The runbooks are §3.6
(GitHub) and §3.3 (Play) of `docs/iterating-and-shipping.md`. Both copy their
artifact to the repo root unless `--out-dir` says otherwise; **use `--out-dir`
with a scratch directory for any build signed with a throwaway key**, so it can
never be the file that gets uploaded.

The default build is a release-mode build signed with the debug key — the same
key Gradle has always used for debug builds, so `adb install -r` replaces an
earlier debug install. If `~/.android/debug.keystore` is ever regenerated, the
next install is treated as a *different app* and fails with
`INSTALL_FAILED_UPDATE_INCOMPATIBLE`; uninstall first. That wipes the WebView
cookie jar, so you sign in again.

`.gitignore` excludes `*.keystore`, `*.jks`, `*.pass` and `keystore.properties`.
**Back the release keystore up somewhere outside this repo.** It is not
recoverable. Losing it means you can never ship an upgrade to an
already-installed sideloaded build, and on Play it means requesting an
upload-key reset.

### Google Play's requirements, and the gates

Enforced by the build, and re-checked by `scripts/build-apk` on the artifact
that will actually ship:

- **Signature** — `apksigner verify` for an APK; a `META-INF/*.RSA|EC` block
  for a bundle.
- **16 KB pages** — every `.so`'s `PT_LOAD` alignment is read out of the ELF
  headers, and an APK must also pass `zipalign -c -P 16`. Our library links with
  `-z max-page-size=16384`; Qt 6.11's are built that way already. Checked, not
  trusted.
- **The manifest** — package `com.kromakobra.cedarview`, minSdk 28, targetSdk
  36, `enableOnBackInvokedCallback="false"`, not debuggable, and
  `[INTERNET]` as the whole permission list.
- **Download size** — for a bundle, `bundletool build-apks` then `get-size`,
  against Play's 200 MB cap.

### The versionCode

Set in `CMakeLists.txt` from `project(VERSION)` with python-for-android's
formula — `"10"` + minSdk + the version folded two digits per part, so 0.1.0
is `1028100` — which keeps every release above the Python-built APKs already
installed from GitHub. Bump the version in `project()` to ship an update.

---

## Debugging on-device

```bash
adb logcat -c                     # clear first, or you drown
adb logcat | grep -iE 'mycu|qml|cedarview'
```

The app's log categories are all `mycu.*` (`mycu.login`, `mycu.chapel`, …), and
Qt routes them to logcat by itself.

A reinstall now replaces everything: the QML is compiled into the library, so
there is no unpacked copy on the device to go stale. (Under python-for-android
the code lived in `files/app/` and was only re-extracted when a stamp changed,
which made `adb install -r` silently keep old QML; that whole class of problem
is gone.)

### Things to check on a device

| Where | What to check |
|---|---|
| sign-in | That the Microsoft page appears in the surface, and that landing back on Self-Service hides it and fills the Summary. |
| `src/platform/android.cpp` | Whether the federated logout round trip really drops the Self-Service cookie, not just the Entra one. |
| transport, end to end | That a large body survives `runJavaScript` on QtWebView. It is verified at 300 KB on QtWebEngine (`tests/tst_webview_transport.cpp`); the system WebView's marshalling limits are not documented. If it truncates, chunk the body in the read script. |
| `HttpGet.java` | That the Chucks and Chapel tabs fill in — they come through the JNI HTTPS path, new with the C++ build. |
| `qml/Main.qml`, edge to edge | The ribbon sits below the status bar with its colour behind it, and the tab bar above the gesture handle. |
| status-bar icons | Light and dark themes each get contrasting icons (driven by `styleHints()->setColorScheme`). |
| back gesture | Back behaves as before at targetSdk 36, via `enableOnBackInvokedCallback="false"`. |
| session metadata | On the first launch after upgrading from a Python build, the theme and the "signed in before" copy may reset once (the state directory moved from p4a's to `<filesDir>/mycu`). The login cookie itself lives in the system WebView and survives. |

Anything about Qt's *API shape* is answerable offline, from the kit:
`.qt/6.11.1/android_arm64_v8a/qml/QtWebView/plugins.qmltypes` lists
`LoadStatus`, `loadingChanged(QQuickWebViewLoadRequest)` and the request's
`url`/`status`/`errorString`, which is what `WebSurfaceAndroid.qml` uses.

### Cookies over JNI — possible now, still not needed

Under PySide6 the idea of reading the session cookie through
`android.webkit.CookieManager` was ruled out: the Android PySide6 build had no
JNI classes at all. In C++ `QJniObject` is right there (`HttpGet` uses it). The
WebView transport stays anyway — it is one code path on both platforms, it
works, and harvesting an `HttpOnly` cookie to replay it elsewhere is exactly
what the design avoids having to do.
