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
`assets/private.tar` into `/data/data/org.mycu.mycu/files/app/` and Qt reads
the `.qml` files from there as ordinary text. So you can overwrite one:

```bash
adb push mycu/ui/qml/SummaryView.qml /data/local/tmp/SummaryView.qml
adb shell run-as org.mycu.mycu cp /data/local/tmp/SummaryView.qml \
    files/app/mycu/ui/qml/SummaryView.qml
adb shell am force-stop org.mycu.mycu
adb shell monkey -p org.mycu.mycu -c android.intent.category.LAUNCHER 1
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
adb shell "run-as org.mycu.mycu sh -c 'cat > files/app/mycu/ui/qml/SummaryView.qml'" \
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
adb shell run-as org.mycu.mycu cp /data/local/tmp/dining.pyc \
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
hammer anyway, `adb shell pm clear org.mycu.mycu` still works — and still signs
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

## 3.2 Google Play: three concrete blockers

I checked the build rather than guessing, and there are real obstacles.

### Blocker 1 — you target API 31, Play requires far newer

Verified in the generated `build.gradle` and manifest:

```
minSdkVersion  21
targetSdkVersion 31          ← Android 12
compileSdkVersion 31
```

`android.api` is not set anywhere, so buildozer falls back to its own default
(`ANDROID_API = '31'` in `buildozer/targets/android.py`). Play will not accept
new apps or updates targeting anything that old — the rule is roughly "within
a year of the current Android release", which as of late 2025 means **API 35**.

Fixing it means pinning `android.api = 35` into `buildozer.spec` via the same
block in `scripts/build-apk` that pins permissions — and then finding out
whether Qt 6.11, p4a and the WebView bootstrap are happy at that level. Assume
that is an afternoon, not a line.

*Check the current required level before you start; Google raises it annually.*

### Blocker 2 — size

148 MB, and it is mostly irreducible: Qt 6.11 for Android plus a full CPython
and its stdlib. Play caps the **download** size generated from a bundle, and
you are in the neighbourhood of that cap.

Do not guess at this. Build the AAB and measure:

```bash
bundletool get-size total --apks=out.apks
```

*The cap has moved over the years — look it up rather than trusting a number
from me.*

### Blocker 3 — the testing requirement

Individual (non-organisation) developer accounts registered after late 2023
must run a **closed test with ~12 opted-in testers for 14 continuous days**
before they can apply for production access. Plus a **$25 one-time**
registration fee.

*Verify the current numbers — this policy is recent and has been adjusted.*

## 3.3 What publishing would actually involve

1. Register a Play Console account, pay the $25.
2. Raise `targetSdkVersion` and re-test on-device — including the sign-in
   WebView, which is the part most likely to break on a new API level.
3. Switch `[buildozer] mode` from `debug` to `release` in `pysidedeploy.spec`.
   The spec's own comment states the consequence: *"release creates a .aab,
   while debug creates a .apk"*. Play requires the AAB.
4. Generate an upload keystore with `keytool` and **back it up somewhere you
   will still have in five years** — lose it and you can never update the app
   under that listing again. Sign by exporting the four variables p4a's
   `build.tmpl.gradle` reads:

   ```
   P4A_RELEASE_KEYSTORE            P4A_RELEASE_KEYSTORE_PASSWD
   P4A_RELEASE_KEYALIAS            P4A_RELEASE_KEYALIAS_PASSWD
   ```

   The artifact lands in `dists/mycu/build/outputs/bundle/release/`.
5. Write a privacy policy (a URL is mandatory) and fill in the Data Safety
   form. Yours is an easy one to answer honestly: the app stores a session
   locally, sends nothing anywhere, and — by design — never sees your password.
6. Store listing: title, short and full description, feature graphic, and at
   least two screenshots.

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
| **Play — Internal testing** | $25 | Up to 100 testers by email, and it sidesteps §3.2's blocker 3 entirely: the 14-day closed-test rule gates *production*, not internal testing. Still needs the AAB, the signing key and the target-SDK bump. **This is the sweet spot if you want proper install-and-update plumbing for a campus-sized audience.** |
| **Obtainium** | none | Users point it at your GitHub releases and get automatic updates. Very little work for you; requires your users to install one extra app. |
| **F-Droid** | none | Realistically not an option — it wants a reproducible build from source in their infrastructure, and this toolchain cross-compiles CPython and Qt. |

**My recommendation:** a signed release APK on GitHub Releases now, and Play
internal testing if and when you want update-delivery to handle itself. Full
Play production is three real obstacles deep for an audience that is one
campus wide.
