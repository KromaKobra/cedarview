# Android: status, and what is left

**Updated 2026-09-23, after the move from Python to C++.**

`docs/android.md` is the reference manual for the toolchain; this file is the
state of play and the to-do list.

---

## 1. The headline

**The app is C++ and QML, and the Android build is Qt's own.** The APK holds
Qt's libraries and `libcedarview` — no CPython, no PySide6, no buildozer or
python-for-android in the build. It went from ~150 MB to 24 MB (21 MB to
download from Play), and `scripts/build-apk` from ~1,400 lines of workarounds
to one configure-and-build plus the gates.

Verified **off the device**, on 2026-09-23:

- `scripts/build-apk` builds a signed debug APK from clean, and the gates pass:
  every one of the 76 native libraries 16 KB-aligned (ELF and zip),
  `com.kromakobra.cedarview`, minSdk 28, targetSdk 36, back-callback opt-out,
  not debuggable, `INTERNET` the only permission.
- `--aab` with a throwaway key (built into a scratch directory, never the repo
  root) passes the same gates plus `bundletool`'s size check: 21 MB largest
  download.
- The whole test suite — 14 Qt Test suites, including the in-page fetch against
  a real (desktop) browser — passes.
- `cedarview --demo` renders every tab pixel-identical to the Python app's.
- The desktop app reaches the dining and chapel-schedule APIs over HTTPS and
  opens Microsoft's sign-in on an empty session.

**Not yet run on the phone.** Everything Android-specific that only a device can
answer is listed in §3. The previous, Python build of the same QML and design
did run on the phone (a moto g power 5G, Android 15) — see §4 for what that
established.

---

## 2. How it builds

```bash
nix develop --command cedarview-android-build -c './scripts/build-apk --install'
```

`qt-cmake` from the Qt 6.11.1 Android kit configures `CMakeLists.txt`; the
`apk`/`aab` target runs `androiddeployqt` and Gradle. The kits come from
aqtinstall into `.qt/`; the SDK and NDK r27c are the ones the Python build
already downloaded. Details in `docs/android.md`.

---

## 3. Next steps, in order

### 1. Install and sign in (only you can do it)

```bash
nix develop --command cedarview-android-build -c './scripts/build-apk --install'
adb logcat -c && adb logcat | grep -iE 'mycu|qml|cedarview'
```

The debug build is signed with `~/.android/debug.keystore`, the key Gradle used
for the Python debug builds, so it installs over one. If the phone has a
release-signed build instead, `adb install` refuses with
`INSTALL_FAILED_UPDATE_INCOMPATIBLE`; uninstall that first (it costs a sign-in).

What "working" looks like in logcat: `login: Sign in with your Cedarville
account`, Microsoft's page on screen, and after you authenticate, `login: `
(blank) followed by `chapel: N ledger entries` and `meal plan: N meals`.

### 2. Check the things only a device can answer

| What | Why it needs the phone |
|---|---|
| Sign-in completes and the Summary fills in | QtWebView is not in the desktop build |
| Chucks and the Chapel schedule fill in | They come through `HttpGet.java` over JNI, new with the C++ build |
| A large body survives `runJavaScript` on QtWebView | Verified at 300 KB on QtWebEngine only |
| Edge to edge: ribbon under the status bar, tab bar above the gesture handle | Needs real insets |
| Status-bar icons contrast in both themes | Needs the real system bars |
| Back gesture behaves at targetSdk 36 | Needs the real system |
| Sign out drops the Self-Service cookie, not just the Entra one | QtWebView has no cookie API |
| Theme and "signed in before" after the upgrade | The state directory moved from p4a's to `<filesDir>/mycu`; a one-time reset is expected, the login cookie is not affected |

### 3. The first real `--aab`

With `~/keys/cedarview-release.jks` — the runbook is §3.3 of
`docs/iterating-and-shipping.md`. The build has only ever been signed with a
throwaway key.

### 4. Play Console

Upload key, privacy policy URL, data safety, and the 12-tester closed test —
all in §3.3 of `docs/iterating-and-shipping.md`.

---

## 4. What the Python build established on the phone, and still holds

The QML, the transport design and the login state machine are unchanged by the
port, so these findings carry over.

**QtWebView's `url` is the URL that was *requested*, not the one the browser
ended up on.** A server-side redirect — exactly how the SAML flow begins —
never updates it. With `currentUrl` bound to it, the app sat on the Chapel tab
with `Couldn't reach Self-Service: TypeError: Failed to fetch` and no sign-in
surface, while logcat showed Chromium refusing a fetch *from*
`login.microsoftonline.com`. Two independent fixes, both still in place:

1. `WebSurfaceAndroid.qml` sets `currentUrl` from `loadRequest.url` in
   `onLoadingChanged`, the only carrier of the post-redirect URL.
   `tests/tst_surfaces.cpp` asserts it, and that `currentUrl` is never bound to
   `view.url` again.
2. `WebViewTransport::get()` treats a *refused* request as a probable sign-in:
   it asks the page for `window.location.href`, which is always the truth, and
   throws `SessionExpired` if that is a sign-in page.

**Android has no OpenSSL default trust store.** Under Python every direct HTTPS
request failed with `CERTIFICATE_VERIFY_FAILED` until the app went looking for
the device's CA directory by hand. The C++ build sidesteps the question: on
Android the direct requests go through `HttpsURLConnection`, which uses the
phone's own trust store.

**Every icon is drawn, not typed.** "↻" rendered as a tofu box on the phone's
Roboto; `Glyph.qml` draws every icon on a `Canvas`.

**adb needs no NixOS configuration** — logind already grants the seat's user an
ACL on the phone's USB node.

The Python toolchain's own failure modes — Python 3.11 pins, autotools,
`ACLOCAL_PATH`, ncurses headers, shiboken's `DT_NEEDED` on `libpython3.11.so`,
the dotted staging directory, the `SOURCE_DATE_EPOCH` extraction stamp, the
random Qt library load order — are gone with the toolchain. They are in the git
history of this file (before 2026-09-23) if anyone ever needs them.

---

## 5. If you change one thing, change it here

- **Qt version** → `QT_VERSION` in `scripts/build-apk` *and* the devShell in
  `flake.nix` / `flake.lock`. They must match. Check the NDK the new kit was
  built with (`lib/cmake/Qt6/qt.toolchain.cmake` in the kit).
- **Target or minimum SDK** → `CMakeLists.txt` (`QT_ANDROID_*_SDK_VERSION`, and
  `CEDARVIEW_MIN_SDK` for the versionCode) *and* `TARGET_SDK`/`MIN_SDK` in
  `scripts/build-apk`, which the gate checks against.
- **Target ABI** → `QT_ANDROID_ABIS` in `CMakeLists.txt`, and the kit
  `scripts/build-apk` fetches.
- **Permissions** → `android/AndroidManifest.xml`. The gate fails anything but
  `INTERNET`; change the gate's expectation in `scripts/build-apk` in the same
  commit, deliberately.
- **Version** → `project(CedarView VERSION …)` in `CMakeLists.txt`. That is the
  only place.
