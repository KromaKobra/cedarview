# Iterating, branding, and shipping

Three questions, answered against *this* toolchain rather than in general.

---

# 1. Not rebuilding the APK every time

## 1.1 The short answer: don't use the phone

`./build/cedarview --demo` is the development loop. It runs the whole app —
QML, viewmodels, list models, the lot — against `tests/fixtures/`, with no
network, no login and no device.

```bash
nix develop
cmake -B build -G Ninja      # once
cmake --build build && ./build/cedarview --demo
```

The QML is compiled into the binary (qmlcachegen, via `qt_add_qml_module`), so
a QML edit is a rebuild — an incremental one, a few seconds, since only the
changed file is recompiled. **This is the answer to your question 99% of the
time.** The UI was built this way; the phone gets touched at the end.

### Seeing it without a display

The app renders fine under `QT_QPA_PLATFORM=offscreen`. To look at a screen
from a script, load the app the way `src/ui/main.cpp` does and, on a timer,
call `grabWindow()` on the root `QQuickWindow` and `save()` it. Set
`currentPage` on the window between grabs to walk the tabs. Resize on a timer
rather than immediately: the offscreen platform resizes the window to its
virtual screen after load, so a size set before the event loop runs is silently
overwritten.

### What demo mode cannot tell you

* Anything behind the sign-in — the skip ledger and the meal-plan balances.
  Fixtures stand in for both.
* Font metrics and touch target sizes as they land on the actual phone.
* Whether a glyph exists in the device's font. This is why every icon in the
  app is drawn on a `Canvas` (`Glyph.qml`) — see `docs/architecture.md`.
* Fixture staleness. `tests/fixtures/` holds two days of menus dated
  2026-09-16/17, so "the next sitting" is empty in demo mode once real time
  moves past them. That is the app being honest, not a bug.

## 1.2 The real app on the desktop

`./build/cedarview` is the real thing — QtWebEngine, the real sign-in, live
data — and it is the same C++ and QML as the APK. Everything but QtWebView
itself can be checked here first.

## 1.3 Rebuilding the APK

```bash
nix develop --command cedarview-android-build -c './scripts/build-apk --install'
```

The first build fetches the Qt kits and Gradle (§2 of `docs/android.md`);
after that a rebuild is incremental — the changed C++ and QML recompile, and
Gradle repackages — and takes about a minute. `--clean` throws `build-android/`
away; pass it only when the build itself is broken.

A reinstall replaces everything. There is nothing unpacked on the device to go
stale: the Python build kept its code in `files/app/`, re-extracted only when a
stamp changed, which made `adb install -r` silently keep old QML; the C++ build
has no such directory.

### When a build "changes nothing"

`scripts/build-apk` only accepts an artifact **newer than a stamp** written
just before the build runs. A build that produces nothing fails loudly rather
than picking up the previous one — which, under the old build, once reinstalled
a known-crashing APK from forty minutes earlier and made two correct fixes look
like they had done nothing.

---

# 2. The launcher icon

## 2.1 What you have now

Three files in `assets/`, the single source for the launcher icon:

| File | Role |
|---|---|
| `appicon_foreground.png`, `appicon_background.png` | the adaptive icon's two layers (Android 8+) |
| `appicon.png` | the flat fallback for anything older |

`CMakeLists.txt` copies them into the Android package as `mipmap/icon*.png`
at configure time, beside `android/res/mipmap-anydpi-v26/icon.xml`, which
composes the two layers. The manifest names `@mipmap/icon`.

This is **not** the tree in the app's header bar. That is `qml/icon.png`,
loaded by `Main.qml` and compiled into the binary with the QML. Changing one
does not change the other; keep them visually related by hand.

### Requirements

| | |
|---|---|
| Format | **PNG**, 32-bit, with alpha |
| Size | **512×512** square |
| Content | Keep the mark inside the middle ~66% — launchers mask corners |

## 2.2 Why the mark looked small, and the geometry that fixes it

Worth writing down, because the obvious diagnosis is wrong.

`assets/appicon.png` is a rounded-square tile that fills its 512×512 canvas —
measured, zero transparent margin. So when the launcher showed the tree small
inside a larger rounded shape, **there was no padding in the file to remove**
and scaling the art up would have achieved nothing.

The shrinking is Android's *legacy icon treatment*. With no adaptive layers, the
system takes the single flat icon, normalises it into the 66% safe zone, and
draws its own backdrop behind it. Art that is itself a rounded tile therefore
renders as a tile inside a tile, with the actual mark down around 45% of the
visible icon.

The geometry that matters for the two-layer format:

| | |
|---|---|
| Layer canvas | 108×108dp — both layers, same size |
| Visible after masking | central **72dp**, i.e. **66.7%** of the canvas |
| Safe for key content | central ~66dp — a circular mask bites the corners of the 72dp square |

Two consequences, and they are the whole recipe:

* **The background must be full-bleed.** Its outer ring is parallax margin that
  is never seen, so a background with its own rounded corners just reintroduces
  the original problem one layer down.
* **The foreground is sized as a fraction of the canvas, not of the visible
  area.** `scripts/make-icon-layers` has one knob, `TREE_FRACTION`, currently
  `0.56` — the tree's longest side at 56% of 512px, which is ~84% of the
  visible icon. It trims the source art's own transparent margin first, so the
  fraction means the tree and not the tree plus whatever padding the file came
  with.

Regenerate and eyeball the masks:

```bash
QT_QPA_PLATFORM=offscreen python scripts/make-icon-layers assets
```

It also writes `assets/_preview.png`, the composite under a circular and a
squircle mask. Look at it before rebuilding, then delete it; it is a check, not
an asset.

The icon is a compiled Android resource, so changing it needs an APK rebuild;
a plain `adb install -r` of the result picks it up.

---

# 3. Distribution

## 3.1 Sideloading

```bash
adb install -r build-android/cedarview-<version>-arm64-v8a-debug.apk
```

For a personal app on your own phone, **this is the whole answer.** Anything
you hand to someone else should be a `--release` build (§3.6).

## 3.2 Google Play: what it takes, and what the build enforces

Audited on 2026-09-22 against Play's rules as of that date. Every item was a
hard blocker for the Python build; every one is met by the C++ build and
**checked on the built artifact**, so a regression fails `scripts/build-apk`
instead of failing a Play upload.

| Requirement | Now | Where |
|---|---|---|
| Target API ≥ 36 (new apps and updates since 2026-08-31) | **36** | `QT_ANDROID_TARGET_SDK_VERSION` in `CMakeLists.txt` |
| Every 64-bit `.so` 16 KB-aligned (since Nov 2025) | all of them | `-z max-page-size=16384`; Qt 6.11's own are built that way |
| App Bundle, not APK | `--aab` | `scripts/build-apk` |
| minSdk the runtime supports | **28** (Qt 6.11's floor) | `QT_ANDROID_MIN_SDK_VERSION` |
| Edge-to-edge (forced from API 35) | safe-area insets | `Main.qml` |
| Privacy policy, in the listing **and** in the app | `PRIVACY.md`, ⋮ menu | — |

### 16 KB pages

Android 15 devices can run with 16 KB memory pages, and a library whose ELF
LOAD segments are aligned for 4 KB does not load on one. Play rejects the
upload outright. Under Python this was most of the work — every library
python-for-android compiled, and a rebuilt `libshiboken6` because Qt's wheels
shipped it 4 KB-aligned in every 6.11 release. In C++ it is one link flag on
our own library; Qt 6.11's official Android binaries are 16 KB already.

The gate still checks rather than trusts: after every build it reads every ELF
in the package and fails on anything below 16 KB, and an APK must also pass
`zipalign -c -P 16`.

### Target API 36 — what changes at runtime

- **Edge to edge.** From API 35 the window draws under the status bar and the
  gesture handle, and at 36 the opt-out is gone. Since Qt 6.9, ApplicationWindow
  pads its content by the safe-area margins, but not its `header` or `footer`.
  `Main.qml` gives those two, and the tab bar, their insets explicitly, so the
  ribbon colour fills in behind the system bars. `main.cpp` sets Qt's colour
  scheme from the in-app theme so the status-bar icons contrast with it.
- **Predictive back.** At 36, Android stops delivering the back key to apps
  that have not moved to `OnBackInvokedCallback`. The manifest carries
  `android:enableOnBackInvokedCallback="false"`, which keeps back behaving as
  it did; the gate checks it.
- **Large screens** (≥ 600 dp) ignore the portrait lock at 36. The layout
  stretches; nothing breaks.

### Size

About 21 MB to download (the Python build was ~150 MB). Play caps the
compressed download of the base module at 200 MB; `--aab` measures it with
`bundletool get-size total` on every build and fails over the cap.

### The testing requirement — not a software fix

Individual developer accounts registered after late 2023 must run a **closed
test with 12 opted-in testers for 14 continuous days** before they can apply
for production. *Check the current numbers — this policy has been adjusted
more than once.* Internal testing (up to 100 testers) is not gated by it.

## 3.3 Publishing to Play — the runbook

```bash
# 1. Bump the version. versionCode = "10" + minSdk + the version folded two
#    digits per part, so 0.2.0 at minSdk 28 is 1028200. Play refuses a re-used one.
$EDITOR CMakeLists.txt           # project(CedarView VERSION 0.2.0 …)

# 2. Build the bundle, signed with your UPLOAD key.
export QT_ANDROID_KEYSTORE_PATH=$HOME/keys/cedarview-release.jks
export QT_ANDROID_KEYSTORE_ALIAS=cedarview
export QT_ANDROID_KEYSTORE_STORE_PASS=...
export QT_ANDROID_KEYSTORE_KEY_PASS=...
nix develop --command cedarview-android-build -c './scripts/build-apk --aab'

# 3. Try it exactly as Play would install it (phone on USB):
nix develop --command cedarview-android-build -c \
  'bundletool install-apks --apks=cedarview-<version>.apks'
```

(The `P4A_RELEASE_KEYSTORE`, `P4A_RELEASE_KEYALIAS`,
`P4A_RELEASE_KEYSTORE_PASSWD` and `P4A_RELEASE_KEYALIAS_PASSWD` names from the
Python build still work.)

`--aab` writes `cedarview-<version>.aab` (upload this) and
`cedarview-<version>.apks` (the split APKs Play would serve, for local
testing). It fails the build unless the manifest says
`com.kromakobra.cedarview`, targetSdk 36, minSdk 28, not debuggable, the
back-callback opt-out, and `INTERNET` as the only permission; unless the bundle
is signed; unless every native library is 16 KB-aligned; and unless the
download fits under 200 MB.

**Only ever build the upload `--aab` with your real key.** The first bundle you
upload fixes the upload key. To exercise the build with a throwaway key, pass
`--out-dir` a scratch directory so that bundle can never be the one uploaded.

In Play Console:

1. **App signing.** Play App Signing is mandatory for new apps. The simplest
   choice is to let Google generate the app-signing key and register
   `~/keys/cedarview-release.jks` as the **upload** key. The applicationId is
   new, so there is no existing install base to stay key-compatible with.
2. **Privacy policy URL**:
   `https://github.com/KromaKobra/cedarview/blob/main/PRIVACY.md`. The app links
   the same page from its ⋮ menu, which Play's User Data policy also requires.
3. **Data safety**: no data collected or shared. Everything is fetched from
   Cedarville to the device and stays there. No account creation, so the
   account-deletion requirement does not apply.
4. **App bundle explorer**, after upload: confirm "16 KB page size: supported".
5. Content rating questionnaire, store listing, screenshots.

## 3.4 Two things worth thinking about before you do

**It is an unofficial client for one university's portal.** Publishing it
publicly is a different act from running it on your own phone. A public listing
using Cedarville's name, signing in to Cedarville's SSO, is the kind of thing a
university IT department may have opinions about, and their name and marks are
theirs. Worth a conversation with them before a listing, not after.

**Play's value here is discovery, and your audience is one campus.** You do not
need a global storefront to reach a few hundred Cedarville students.

## 3.5 The alternatives

| Route | Cost | Notes |
|---|---|---|
| **Sideload the APK** | none | Fine for you and a few friends. |
| **GitHub Releases** | none | Attach the APK to a tag. Link it. People tap and install. Sign it with a stable release key so updates install over each other. |
| **Play — Internal testing** | $25 | Up to 100 testers by email, and it sidesteps the 14-day closed-test rule (§3.2), which gates *production*, not internal testing. Uses the same `--aab` build as production. **The sweet spot for proper install-and-update plumbing for a campus-sized audience.** |
| **Obtainium** | none | Users point it at your GitHub releases and get automatic updates. Very little work for you; requires your users to install one extra app. |
| **F-Droid** | none | Possible in principle now that the build is plain CMake, but F-Droid builds from source on its own infrastructure and wants no prebuilt binaries — the Qt kits come from download.qt.io, which it would build itself. A project of its own. |

## 3.6 Releasing on GitHub — the runbook

### Why a debug build must not be the thing you publish

**The debug key is not yours to keep.** It is `~/.android/debug.keystore`,
generated by the SDK and regenerated without ceremony if it is ever lost.
Android refuses to install an update signed by a different key, so the day that
file changes, every user has to uninstall — losing their session — to take an
update.

### One-time: make a keystore, and do not lose it

```bash
keytool -genkeypair -v \
  -keystore ~/keys/cedarview-release.jks \
  -alias cedarview -keyalg RSA -keysize 4096 -validity 10000
```

**Back this file and its passwords up somewhere you will still have in five
years.** Lose it and you cannot ship an update that installs over what people
already have — the only route left is telling everyone to uninstall first.
Do not commit it; `~/keys/` or a password manager, not the repo.

### Each release

```bash
# 1. Bump the version. versionCode is derived from it, and two releases that
#    share a versionCode are not an upgrade pair.
$EDITOR CMakeLists.txt           # project(CedarView VERSION 0.2.0 …)

# 2. Build signed.
export QT_ANDROID_KEYSTORE_PATH=$HOME/keys/cedarview-release.jks
export QT_ANDROID_KEYSTORE_ALIAS=cedarview
export QT_ANDROID_KEYSTORE_STORE_PASS=...
export QT_ANDROID_KEYSTORE_KEY_PASS=...
nix develop --command cedarview-android-build -c './scripts/build-apk --release'
```

`--release` refuses to start if any signing variable is missing, and refuses to
finish unless `apksigner` verifies the result. The artifact is
`cedarview-<version>-arm64-v8a.apk` in the project root — named apart from the
debug build on purpose, since the two are signed with different keys and
publishing the wrong one is a mistake you only get to make once.

Then:

```bash
gh release create v0.2.0 cedarview-0.2.0-arm64-v8a.apk \
  --title "CedarView 0.2.0" --notes "..."
```

### What your users will see

* **"Install unknown apps"** — they must allow it for whichever browser or file
  manager they download with. One-time, per-app, in Settings.
* **Play Protect warning** — "unrecognised developer", because the key is not
  known to Google. They tap through it. This does not go away by signing
  properly; it goes away by shipping through Play.
* **arm64 only.** `QT_ANDROID_ABIS arm64-v8a` in `CMakeLists.txt`, and the APK
  carries `lib/arm64-v8a/` alone. Every current Pixel, Samsung and Motorola
  phone is arm64, so this is fine in practice — but a budget `armeabi-v7a`
  device will refuse to install, with an unhelpful message. A second ABI is a
  second Qt kit and a larger APK.
* **`minSdkVersion 28`, `targetSdkVersion 36`** — Android 9 through current,
  which is the range Qt 6.11 supports.
* **About 24 MB.**

### Updates

Users reinstall over the top, keeping their session, **provided every release
is signed with the same key and has a higher versionCode**. The versionCode
formula is the one python-for-android used, so the first C++ release sorts
above the Python-built ones already installed. Point people at Obtainium (§3.5)
if you would rather not chase them to re-download.

---

## 3.7 The name, and the identity behind it

**The name is CedarView** — the launcher label in `android/AndroidManifest.xml`
and the Qt application name in `main.cpp`.

**The identity is `com.kromakobra.cedarview`,** from the release that first
went to Google Play. Through v0.1.0 it was `org.mycu.mycu`. The change happened
then because Play makes the applicationId permanent at the first upload, and
`org.mycu` named a domain nobody here owns plus Cedarville's portal brand.

To Android an applicationId is the app's identity, not a label: a new one is a
**different app**. Anyone who sideloaded v0.1.0 has two apps after installing
the new one. Uninstalling the old `org.mycu.mycu` one costs a sign-in and
nothing else.

It is set in one place, `QT_ANDROID_PACKAGE_NAME` in `CMakeLists.txt`, and
checked in the built manifest by `scripts/build-apk`. **Do not change it
again.** Play will not let the listing move to a new one.

`mycu` survives in two places that are not part of the identity: the session
directory (`APP_DIR_NAME` in `src/core/session.h`) and the log categories.
Renaming the directory would sign everyone out, which is not worth a tidier
name.
