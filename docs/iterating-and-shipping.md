# Iterating, branding, and shipping

Three questions, answered against *this* toolchain rather than in general.
Everything below was checked against the build tree as it stands on
2026-09-17; where a claim is a policy that changes under you, it says so.

**Marked `[unverified]`** — one step, the `run-as` write in §1.3. The phone came
off USB before it could be tested. Everything else here was run or read out of
the source.

---

# 1. Not rebuilding the APK every time

## 1.1 The short answer: don't use the phone

`python -m mycu --demo` is the development loop, and it is about **two
seconds**. It runs the whole app — QML, viewmodels, list models, the lot —
against `tests/fixtures/`, with no network, no login and no device.

```bash
nix develop
python -m mycu --demo
```

Edit a `.qml`, kill it, run it again. There is no hot reload, but there does
not need to be: the QML is read off the filesystem at startup
(`app.py` does `engine.load(QUrl.fromLocalFile(QML_DIR / "Main.qml"))`), so a
restart *is* the reload.

**This is the answer to your question 99% of the time.** The entire UI
overhaul was built this way; the phone was touched once, at the end.

### Seeing it without a display

The app renders fine under `QT_QPA_PLATFORM=offscreen` with the software
renderer, which means you can grab a PNG of a screen from a script instead of
squinting at a phone:

```bash
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software python -m mycu --demo
```

To actually capture a frame, build the object graph the way `app.py` does, load
`Main.qml`, then on a timer call `grabWindow()` on the root object and `.save()`
it. Two gotchas, both of which cost time to find:

* **Import `PySide6.QtQuick` at module scope.** Without it the root object
  wrappers as a bare `QWindow`, which has no `grabWindow`, and the cast is
  cached so you cannot fix it afterwards.
* **Resize on a timer, not immediately.** The offscreen platform resizes the
  window to its virtual screen after load, so a size set before the event loop
  runs is silently overwritten and you get a 972px-wide "phone".

### What demo mode cannot tell you

* Anything behind the sign-in — the skip ledger and the meal-plan balances.
  Fixtures stand in for both.
* Font metrics and touch target sizes as they land on the actual phone.
* Whether a glyph exists in the device's font. This is why every icon in the
  app is drawn on a `Canvas` (`Glyph.qml`) — see `docs/architecture.md`.
* Fixture staleness. `tests/fixtures/` holds two days of menus dated
  2026-09-16/17, so "the next sitting" is empty in demo mode once real time
  moves past them. That is the app being honest, not a bug.

## 1.2 Incremental rebuilds are already much faster

The ~25 minutes you watched was a **cold** build: it cloned python-for-android,
then cross-compiled CPython 3.11.13, OpenSSL and sqlite3 for arm64. None of
that happens again. The state lives in
`android-build/.buildozer/` and is reused.

A warm rebuild is staging + repackage — minutes, not tens of minutes.

**`--clean` throws all of it away.** Only pass it when the build itself is
broken, never as a habit.

## 1.3 Pushing QML straight onto the phone, no build at all

The APK does not execute QML from inside itself. The bootstrap untars
`assets/private.tar` into `/data/data/com.kromakobra.cedarview/files/app/` and Qt reads
the `.qml` files from there as ordinary text. So you can overwrite one:

```bash
adb push mycu/ui/qml/SummaryView.qml /data/local/tmp/SummaryView.qml
adb shell run-as com.kromakobra.cedarview cp /data/local/tmp/SummaryView.qml \
    files/app/mycu/ui/qml/SummaryView.qml
adb shell am force-stop com.kromakobra.cedarview
adb shell monkey -p com.kromakobra.cedarview -c android.intent.category.LAUNCHER 1
```

Seconds instead of minutes, on the real device, with real data.

`run-as` works because debug builds are `android:debuggable`. **It will stop
working the day you build a release APK** — that is the whole point of the
flag.

**`[unverified]`** The read half is proven (`run-as … ls files/app/mycu/ui/qml/`
was run twice during the install). The `cp` write was not — the phone
disconnected first. The mechanism is standard, but there is one plausible
failure: `/data/local/tmp` is `shell:shell` mode `0771`, so the app uid gets
traverse-but-not-list. Pushed files land `0644` and are readable through it on
ordinary devices; a hardened SELinux policy could refuse. If it does, pipe
through stdin instead and skip the shared directory entirely:

```bash
adb shell "run-as com.kromakobra.cedarview sh -c 'cat > files/app/mycu/ui/qml/SummaryView.qml'" \
    < mycu/ui/qml/SummaryView.qml
```

(Text only — `adb shell` can mangle line endings, so never do this with a
binary.)

## 1.4 Python changes: you need a `.pyc`

The tar ships **compiled bytecode only** — 29 `.pyc` files, zero `.py`:

```
main.pyc
mycu/core/providers/dining.pyc
mycu/ui/viewmodels/dining.pyc
…
```

So the same trick needs a compile step, and the bytecode magic number must
match the interpreter in the APK — **CPython 3.11.13**. Two interpreters on
this machine will do it; p4a's own is the exact version:

```
android-build/.buildozer/android/platform/build-arm64-v8a/build/other_builds/\
hostpython3/desktop/hostpython3/native-build/python     # 3.11.13
/nix/store/…-python3-3.11.16/bin/python3.11             # 3.11.16, also fine
```

Any 3.11.x works — the magic number is per *minor* version.

```bash
HP=android-build/.buildozer/android/platform/build-arm64-v8a/build/other_builds/hostpython3/desktop/hostpython3/native-build/python
$HP -c "import py_compile; py_compile.compile('mycu/ui/viewmodels/dining.py', 'dining.pyc')"
adb push dining.pyc /data/local/tmp/
adb shell run-as com.kromakobra.cedarview cp /data/local/tmp/dining.pyc \
    files/app/mycu/ui/viewmodels/dining.pyc
```

Fiddly enough that it is rarely worth it. Python logic is exactly the part that
`pytest` and demo mode cover well — 279 tests, half a second. Save the device
for the things only the device can answer.

## 1.5 When you *must* do a full rebuild

* New or renamed QML files — `qml_files` in `pysidedeploy.spec` feeds the Qt
  module scan.
* A new Qt module or import.
* Permissions, icon, target SDK, app version.
* Anything in `mycu/platform/android.py`.

### `adb install -r` and the stale `files/app/`

This used to say "`adb install -r` does not refresh `files/app/` — the
bootstrap gates extraction on a stamp", and recommended `pm clear`. The gating
is real; the reason it never fired was more specific, and the fix is better than
`pm clear`.

`PythonUtil.unpackAsset` compares a stamp baked into the APK against
`files/app/private.version` and extracts only on a difference. The stamp is

```
sha1("{version} {numeric_version} {timestamp}")          build.py:595
```

and on this machine every part was a constant:

| part | value | from |
|---|---|---|
| `version` | `0.1` | the generated `buildozer.spec` |
| `numeric_version` | `10211` | `"10"` + min_sdk `21` + version_code `1` |
| `timestamp` | `315532800` | **`SOURCE_DATE_EPOCH`** — 1980-01-01 |

That last one is the culprit. Nix's stdenv exports `SOURCE_DATE_EPOCH` for
reproducible builds, `nix develop` inherits it, and `build.py` prefers it over
the wall clock. So the stamp was the same hash on every build, forever:

```
sha1("0.1 10211 315532800") = 463cf0e3c4f3e09a34aa591298bf7001fae14d04
```

— which is exactly what was in `files/app/private.version` on the phone. Install
a new APK, the bootstrap compares equal, skips extraction, and runs the old code.

**`scripts/build-apk` now unsets `SOURCE_DATE_EPOCH` for the buildozer run**, so
each build gets a distinct stamp and each install refreshes itself. It does not
sign you out: `unpackAsset` deletes `files/app` only, and the session lives in
`files/mycu/session.json`. The APK stops being bit-reproducible, which nothing
here relies on for a debug build signed with a throwaway key.

`--install` now prints the before/after stamp and warns loudly if they match.
The confirmation in the log is:

```
adb logcat -c && adb logcat | grep -iE 'python|qml|mycu|Extracting'
```

`Extracting private assets.` means the new code landed. If you ever need the
hammer anyway, `adb shell pm clear com.kromakobra.cedarview` still works — and still signs
you out.

### A rebuild used to be a dice roll

Worth knowing before you trust a fresh APK. `pyside6-android-deploy` computes
the Qt module list as `list(set(modls))`
(`deploy_lib/android/android_config.py:125`), and a set of `str` iterates in an
order that depends on the interpreter's hash seed — randomised per process. That
order is rendered verbatim into `--qt-libs`, into `res/values/libs.xml`, and
into the order `QtLoader` dlopens the Qt libraries at startup.

`libQt6Core`'s `JNI_OnLoad` is what stores the JavaVM pointer the other
libraries read back. If Core is not first, the first library whose `JNI_OnLoad`
touches JNI dereferences a null env:

```
F libc: Fatal signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x0
        in tid NNNNN (qtMainLoopThrea)
#00 QJniEnvironment::getJniEnv()+48   libQt6Core_arm64-v8a.so
#02 JNI_OnLoad+68                     libQt6Quick_arm64-v8a.so
```

It happens on the Qt loader thread before the interpreter starts, so there is no
Python traceback and nothing in the app's own logging — just the native
tombstone. **A source change is not required to trigger it; two builds of
identical code can differ.** This bit on 2026-09-18, with `Core` fourth.

Phase 1a now re-sorts `--qt-libs` into a fixed dependency order with `Core`
first, keeping whatever the scan found and pinning only the sequence. If you
ever see that tombstone again, check `p4a.extra_args` in the generated
`buildozer.spec` first.

### A failed build used to install the previous one

Worse than the bug it hid. `scripts/build-apk` cannot trust buildozer's exit
code — with no TTY it finishes packaging, prints "# Android packaging done!",
then exits non-zero failing to restore terminal state. So the script used the
*presence* of an APK as the success test.

The hole: "an APK exists" stays true when a build produces nothing, because the
previous build's APK is still sitting in `android-build/`. A failed build
therefore copied a stale APK over `mycu.apk`, `--install` pushed it to the
phone, and the script reported success. On 2026-09-18 that reinstalled a
known-crashing APK from forty minutes earlier and made two correct fixes look
like they had done nothing.

The APK is now required to be **newer than a stamp file** written immediately
before buildozer runs. A build that produces nothing fails loudly and says where
the older APK is, instead of shipping it.

**When a build "changes nothing", check this first:**

```bash
ls -la android-build/*.apk mycu.apk        # is the APK actually from this run?
```

### p4a forgets to create `mipmap-anydpi-v26`

Only bites once both adaptive icon keys are set. `build.py` writes
`res/mipmap-anydpi-v26/icon.xml` without an `ensure_dir()` first — the one res
write in that file that lacks it — and the qt bootstrap template ships only
`drawable/` and `mipmap/`. Result: `FileNotFoundError` *after* the two layer
PNGs are copied and *before* `values/libs.xml` is rendered, so the dist is left
half-written and no APK is produced.

Phase 1c creates the directory in the p4a bootstrap template, which every dist
is copied from. On a cold build the clone does not exist yet, so the build
retries itself once after the clone appears.

---

# 2. The launcher icon

## 2.1 What you have now

`assets/appicon.png`, pinned into the generated `buildozer.spec` by phase 1a of
`scripts/build-apk` — the same block that pins the permissions and the p4a
revision. See §2.3 for why it has to be done there and not in a spec file.

This is **not** the tree in the app's header bar. That is
`mycu/ui/qml/icon.png`, loaded by `Main.qml`, and it travels a completely
different road: it rides inside `assets/private.tar` and can be pushed onto a
running device with `run-as` (§1.3), whereas the launcher icon is a compiled
Android resource and cannot be pushed at all. Changing one does not change the
other, and there is no mechanism keeping them consistent — if you want them to
match, that is a thing you do by hand.

## 2.2 What p4a actually does with the file

From `pythonforandroid/bootstraps/common/build/build.py`:

```python
shutil.copy(args.icon or default_icon, join(res_dir, 'mipmap/icon.png'))
```

**A verbatim copy.** No resizing, no format conversion, no density variants.
Two things follow:

1. **Supply a PNG.** Today's `.jpg` is copied to a file *named* `icon.png` that
   contains JPEG bytes. Android sniffs content rather than trusting the
   extension, so it happens to work — but it is wrong on disk and JPEG has no
   alpha, so your icon gets a square white background on every launcher.
2. **Supply one big one.** There is a single file in `mipmap/`, not a set of
   `mipmap-hdpi`/`-xhdpi`/… variants, so Android scales that one image to every
   density. Give it the largest size you care about.

### Requirements

| | |
|---|---|
| Format | **PNG**, 32-bit, with alpha |
| Size | **512×512** square |
| Content | Keep the mark inside the middle ~66% — launchers mask corners |

## 2.3 Where to put it — and why not in the spec

**Editing `icon` in the project's `pysidedeploy.spec` does nothing.** This was
the obvious move and it is wrong, so it is worth being explicit about why.

That file is an **output of the build, not an input**. Phase 1 of
`scripts/build-apk` does `rm -f pysidedeploy.spec buildozer.spec` *inside the
staging tree* and lets `--init` write both from scratch — that is deliberate,
and the header comment explains what it buys (it keeps the tool on the code
path where `ndk_path` and `modules` are handled correctly). At the end of the
build the generated spec is copied back over the project's copy as the record
of what actually shipped. So anything you type into the project's spec is
overwritten, and was never read in the first place.

There is no `--icon` flag on `pyside6-android-deploy` either — checked. It
reads `icon` from the spec it has just generated itself
(`deploy_lib/android/buildozer.py:75` writes `icon.filename` from
`pysidedeploy_config.icon`), and what `--init` writes there is the stock
`pyside_icon.jpg`. There is no route in from outside.

**So the icon is pinned after generation, exactly like the permissions.** It is
one more substitution in the phase 1a Python block:

```python
text, n = re.subn(r'^icon\.filename\s*=.*$', f'icon.filename = {icon}', text, flags=re.M)
```

The path is set by `ICON` at the top of `scripts/build-apk`, and it is checked
for existence in the preconditions — a missing icon fails the build in a second
rather than silently shipping PySide's.

Use an **absolute path**. buildozer resolves `icon.filename` relative to its own
root — `android-build/`, not the project root — so a relative path silently
resolves to the wrong place. This is also why `assets/` does not need to be
staged into `android-build/` for the icon to work: nothing copies it, the path
just points out of the tree.

Then rebuild (a warm one) and `pm clear` is *not* needed — the icon is a real
Android resource, not part of `private.tar`, so a plain `adb install -r` picks
it up. **There is no push-it-onto-the-phone shortcut for this one**; §1.3 works
for QML because Qt reads those files as loose text at runtime, and a launcher
icon is compiled into the APK's resource table.

## 2.4 Adaptive icons — which this build now uses

p4a supports the modern two-layer format, and buildozer exposes it. The exact
keys — these are easy to get wrong, they are not the names the Android docs
use:

```ini
icon.adaptive_foreground.filename = /abs/path/fg.png
icon.adaptive_background.filename = /abs/path/bg.png
```

Both must be set; p4a prints `WARNING: Received an --icon_fg or an --icon_bg
argument, but not both. Ignoring.` and silently falls back if only one is. The
preconditions in `scripts/build-apk` check for both and refuse to build with
one, because that fallback is silent and looks exactly like the icon not having
changed.

Unlike `icon.filename`, these keys are not in the generated `buildozer.spec` at
all — `pysidedeploy.spec` has a single `icon` field and no route for them — so
phase 1a **inserts** them, anchored after `icon.filename` so they land in the
`[app]` section. `icon.filename` stays as the pre-API-26 fallback.

## 2.5 Why the mark looked small, and the geometry that fixes it

Worth writing down, because the obvious diagnosis is wrong.

`assets/appicon.png` is a rounded-square tile that fills its 512×512 canvas —
measured, zero transparent margin. So when the launcher showed the tree small
inside a larger rounded shape, **there was no padding in the file to remove**
and scaling the art up would have achieved nothing.

The shrinking is Android's *legacy icon treatment*. With no adaptive layers, the
system takes the single `mipmap/icon.png`, normalises it into the 66% safe zone,
and draws its own backdrop behind it. Art that is itself a rounded tile
therefore renders as a tile inside a tile, with the actual mark down around 45%
of the visible icon.

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
squircle mask. Look at it before rebuilding — it is a second, not the ten
minutes a warm build costs — then delete it; it is a check, not an asset.

---

# 3. Distribution

## 3.1 What you have right now

A 148 MB debug APK. Anyone can install it by sideloading:

```bash
adb install -r mycu.apk           # over USB
```

or by putting `mycu.apk` somewhere downloadable and letting people tap it —
they will have to allow "install unknown apps" for their browser, and will see
a Play Protect warning because it is signed with the debug key.

For a personal app on your own phone, **this is the whole answer** and I would
stop here.

## 3.2 Google Play: what it took, and what the build now enforces

Audited on 2026-09-22 against the shipped v0.1.0 APK and Play's rules as of that
date. Every item was a hard blocker; every one is now fixed in the build and
**checked on the built artifact**, so a regression fails `scripts/build-apk`
instead of failing a Play upload.

| Requirement | Was | Now | Where |
|---|---|---|---|
| Target API ≥ 36 (new apps and updates since 2026-08-31) | 31, buildozer's default | **36** | `ANDROID_API` in `scripts/build-apk` |
| Every 64-bit `.so` 16 KB-aligned (since Nov 2025) | 73 libraries at 4 KB | all 16 KB | phases 1d and 1f |
| App Bundle, not APK | APK forced | `--aab` | phase 1a |
| minSdk the runtime supports | 21 | **28** (Qt 6.11's floor) | `ANDROID_MINAPI` |
| Edge-to-edge (forced from API 35) | header under the status bar | safe-area insets | `Main.qml` |
| Privacy policy, in the listing **and** in the app | none | `PRIVACY.md`, ⋮ menu | — |

The ones worth knowing about in detail:

### 16 KB pages — the one that needed real work

Android 15 devices can run with 16 KB memory pages, and a library whose ELF
LOAD segments are aligned for 4 KB does not load on one. Play rejects the
upload outright. Qt's own libraries were fine. The 4 KB ones were:

- **Everything python-for-android compiles**: `libpython3.11`, `libssl`,
  `libcrypto`, `libffi`, `libsqlite3`, `libmain`, and 66 CPython extension
  modules inside `libpybundle.so`. NDK r27c links for 4 KB unless told
  otherwise. Phase 1d patches p4a to pass `-z max-page-size=16384` through
  `archs.py` (configure-based recipes) and `APP_SUPPORT_FLEXIBLE_PAGE_SIZES=true`
  on the ndk-build command lines (sqlite3, libmain).
- **`libshiboken6.abi3.so` and `Shiboken.abi3.so` from Qt's own wheel.** Every
  6.11 release ships them 4 KB-aligned, and the segments share 16 KB pages, so
  patching the header cannot fix it. `scripts/android/recipes/shiboken6`
  rebuilds both from the pyside-setup 6.11.0 source, with the same NDK and API
  level Qt used plus the 16 KB flag. The host generator comes from nixpkgs
  (`SHIBOKEN6_HOST_PATH`, set by `flake.nix`). Before installing them, the
  recipe **refuses unless the rebuild exports every symbol the original does**
  (448 of 448) with identical SONAME, NEEDED and RUNPATH, because `libpyside6`
  and every Qt binding link against it.

The NDK stays at r27c on purpose: Qt 6.11 was built with it, and the
`libc++_shared.so` the PySide6 recipe copies from it is already 16 KB-aligned.

The gate: after every build, `scripts/build-apk` reads every ELF in `lib/` —
and inside `libpybundle.so`, which Play's own checker does not open but the
phone's loader does — and fails on anything below 16 KB. A build that reuses
libraries compiled before the flags existed fails there. Rebuild with `--clean`.

### Target API 36 — what changes at runtime

- **Edge to edge.** From API 35 the window draws under the status bar and the
  gesture handle, and at 36 the opt-out is gone. Since Qt 6.9, ApplicationWindow
  pads its content by the safe-area margins, but not its `header` or `footer`.
  `Main.qml` gives those two, and the tab bar, their insets explicitly, so the
  ribbon colour fills in behind the system bars. `app.py` sets Qt's colour
  scheme from the in-app theme so the status-bar icons contrast with it.
- **Predictive back.** At 36, Android stops delivering the back key to apps
  that have not moved to `OnBackInvokedCallback`, and Qt 6.11 has not. The
  manifest carries `android:enableOnBackInvokedCallback="false"`
  (patched in by `scripts/build-apk` phase 1g), which keeps back behaving as
  it did.
- **Large screens** (≥ 600 dp) ignore the portrait lock at 36. The layout
  stretches; nothing breaks.

### Still worth measuring: size

~150 MB, mostly Qt and CPython. Play caps the compressed download of the base
module at 200 MB. `--aab` measures it with `bundletool get-size total` on every
build and fails over the cap.

### The testing requirement — not a software fix

Individual developer accounts registered after late 2023 must run a **closed
test with 12 opted-in testers for 14 continuous days** before they can apply
for production. *Check the current numbers — this policy has been adjusted
more than once.* Internal testing (up to 100 testers) is not gated by it.

## 3.3 Publishing to Play — the runbook

```bash
# 1. Bump the version. versionCode = "10" + minSdk + the dotted version folded
#    into digits, so 0.2.0 at minSdk 28 is 1028200. Play refuses a re-used one.
$EDITOR mycu/__init__.py

# 2. Build the bundle, signed with your UPLOAD key. The first build after
#    pulling these changes must add --clean.
export P4A_RELEASE_KEYSTORE=$HOME/keys/cedarview-release.jks
export P4A_RELEASE_KEYALIAS=cedarview
export P4A_RELEASE_KEYSTORE_PASSWD=...
export P4A_RELEASE_KEYALIAS_PASSWD=...
nix develop --command mycu-android-build -c './scripts/build-apk --aab'

# 3. Try it exactly as Play would install it (phone on USB):
nix develop --command mycu-android-build -c \
  'bundletool install-apks --apks=cedarview-<version>.apks'
```

`--aab` writes `cedarview-<version>.aab` (upload this) and
`cedarview-<version>.apks` (the split APKs Play would serve, for local
testing). It fails the build unless the manifest says
`com.kromakobra.cedarview`, targetSdk 36, minSdk 28, not debuggable, the
back-callback opt-out, and `INTERNET` as the only permission; unless the bundle
is signed; unless every native library is 16 KB-aligned; and unless the
download fits under 200 MB.

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

## 3.5 The alternatives, which fit this app better

| Route | Cost | Notes |
|---|---|---|
| **Sideload the APK** | none | What you are doing. Fine for you and a few friends. |
| **GitHub Releases** | none | Attach the APK to a tag. Link it. People tap and install. Sign it with a stable release key so updates install over each other. |
| **Play — Internal testing** | $25 | Up to 100 testers by email, and it sidesteps the 14-day closed-test rule (§3.2), which gates *production*, not internal testing. Uses the same `--aab` build as production. **This is the sweet spot if you want proper install-and-update plumbing for a campus-sized audience.** |
| **Obtainium** | none | Users point it at your GitHub releases and get automatic updates. Very little work for you; requires your users to install one extra app. |
| **F-Droid** | none | Realistically not an option — it wants a reproducible build from source in their infrastructure, and this toolchain cross-compiles CPython and Qt. |

The software side of Play is done (§3.2): `--aab` produces an upload-ready
bundle. What is left is the Console work in §3.3 and, for production, the
closed-test period. Internal testing is a reasonable first stop while that runs,
and the GitHub APK (§3.6) keeps working alongside either.

---

## 3.6 Releasing on GitHub — the runbook

### Why a debug APK must not be the thing you publish

Two reasons, and the second is the serious one.

1. **The debug key is not yours to keep.** It is `~/.android/debug.keystore`,
   generated by the SDK and regenerated without ceremony if it is ever lost.
   Android refuses to install an update signed by a different key, so the day
   that file changes, every user has to uninstall — losing their session — to
   take an update.

2. **A debug APK is `android:debuggable`.** That is what makes the `run-as`
   trick in §1.3 work, and it means anyone with USB debugging and a cable can
   read the app's private files off the device. This app keeps a live
   Cedarville SSO session in `files/mycu/session.json`. Shipping a debuggable
   build to other students hands that session to anyone who picks up the phone
   with adb available. A release build clears the flag.

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
$EDITOR mycu/__init__.py          # __version__ = "0.2.0"

# 2. Build signed. All four variables or buildozer silently drops --sign.
export P4A_RELEASE_KEYSTORE=$HOME/keys/cedarview-release.jks
export P4A_RELEASE_KEYALIAS=cedarview
export P4A_RELEASE_KEYSTORE_PASSWD=...
export P4A_RELEASE_KEYALIAS_PASSWD=...

nix develop --command mycu-android-build -c "./scripts/build-apk --release"
```

`--release` pins `android.release_artifact = apk` (buildozer defaults a release
to `.aab`, which is the Play upload format — `--aab`, §3.3 — and **cannot be
sideloaded**), pins
`version` from `__version__`, refuses to start if any signing variable is
missing, and refuses to finish if the resulting APK has no signature block.
The artifact is `cedarview-<version>-arm64-v8a.apk` in the project root — named
apart from `mycu.apk` on purpose, since the two are signed with different keys
and publishing the wrong one is a mistake you only get to make once.

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
* **arm64 only.** `arch = aarch64` in `pysidedeploy.spec`, and the APK carries
  `lib/arm64-v8a/` alone. Every current Pixel, Samsung and Motorola phone is
  arm64, so this is fine in practice — but a budget `armeabi-v7a` device will
  refuse to install, with an unhelpful message. Building a second ABI roughly
  doubles the build time and the size.
* **`minSdkVersion 28`, `targetSdkVersion 36`** — Android 9 through current,
  which is the range Qt 6.11 supports.
* **~150 MB.** Well under GitHub's 2 GB per-file limit, but it is a real
  download on campus wifi.

### Updates

Users reinstall over the top, keeping their session, **provided every release
is signed with the same key and has a higher versionCode**. Point them at
Obtainium (§3.5) if you would rather not chase people to re-download.

The first release cannot install over the debug build anyone is currently
running — different key. They uninstall once, then updates are seamless
forever after.

---

## 3.7 The name, and the identity behind it

**The name is CedarView.** Phase 1a pins `title = CedarView` into the generated
`buildozer.spec`, which is the launcher label, and `app.py` sets the Qt
application name to match.

**The identity is `com.kromakobra.cedarview`,** from the release that first
went to Google Play. Through v0.1.0 it was `org.mycu.mycu`. The change happened
then because Play makes the applicationId permanent at the first upload, and
`org.mycu` named a domain nobody here owns plus Cedarville's portal brand.

To Android an applicationId is the app's identity, not a label: a new one is a
**different app**. Anyone who sideloaded v0.1.0 has two apps after installing
the new one. Uninstalling the old `org.mycu.mycu` one costs a sign-in and
nothing else.

It is set in one place, `APP_PACKAGE_NAME` and `APP_PACKAGE_DOMAIN` at the top
of `scripts/build-apk`. `--name` feeds `package.name` (and the p4a dist name),
and phase 1a pins `package.domain`. **Do not change it again.** Play will not
let the listing move to a new one.

The Python package is still `mycu`, and `APP_DIR_NAME` in
`mycu/core/session.py` is still `"mycu"`. That names the directory holding the
session inside the app's private storage, so it is not part of the identity,
and changing it would sign everyone out.
