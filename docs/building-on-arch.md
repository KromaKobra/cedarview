# Building on Arch Linux

The other docs assume NixOS: `nix develop`, and `cedarview-android-build` for
the FHS shell. On Arch neither exists and neither is needed. Arch is already an
FHS system, so the prebuilt NDK, SDK, Gradle and host Qt tools run as they are.
This page is the Arch equivalent of the Nix commands in `docs/dev_cmds.md`,
`docs/android.md` and `docs/iterating-and-shipping.md`. **What each build mode
does, and why, is still in those docs.** This page only covers how to run them
here.

Verified on 2026-11-02: desktop build, `ctest` and a debug APK built with
`scripts/build-apk`. A throwaway-key `--aab` was built too, into a scratch
directory.

---

## 1. One-time setup

### Packages

```bash
# Desktop build (the demo, the tests, the real app on the desktop)
sudo pacman -Syu base-devel cmake ninja qt6-base qt6-declarative qt6-svg qt6-webengine
# Android build
sudo pacman -Syu jdk17-openjdk android-tools
paru -S bundletool        # AUR. Needed for --aab only
```

`qt6-webengine` pulls in `qt6-webchannel` and `qt6-positioning`. That's the
same module list as `qtModules` in `flake.nix`.

**Leave `archlinux-java` set to whatever it is.** Gradle needs Java 17, and
each build command below passes it as `JAVA_HOME`. Changing the system default
would affect other Java apps (Ghidra, for example, needs 21+).

### Android SDK and NDK

Download "Command line tools only" from
<https://developer.android.com/studio#command-line-tools-only>, then:

```bash
SDK=~/.local/share/android/sdk
mkdir -p $SDK/cmdline-tools
unzip ~/Downloads/commandlinetools-linux-*_latest.zip -d $SDK/cmdline-tools
mv $SDK/cmdline-tools/cmdline-tools $SDK/cmdline-tools/latest

export JAVA_HOME=/usr/lib/jvm/java-17-openjdk
$SDK/cmdline-tools/latest/bin/sdkmanager --licenses
$SDK/cmdline-tools/latest/bin/sdkmanager \
  "platforms;android-36" "build-tools;36.0.0" "platform-tools" "ndk;27.2.12479018"
```

That's about 2.5 GB. The NDK **must** be r27c (`27.2.*`). Qt 6.11.1's Android
binaries were built with it, and `scripts/build-apk` refuses any other version.

Then add these lines to `~/.config/zsh/.zshenv` (or wherever your shell's
environment lives) and open a new terminal:

```sh
export ANDROID_SDK_ROOT="$XDG_DATA_HOME/android/sdk"
export ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/27.2.12479018"
```

If you don't set `XDG_DATA_HOME`, write `$HOME/.local/share` in its place. Don't
export `JAVA_HOME` here; see above.

### The release keystore

`~/keys/cedarview-release.jks`, alias `cedarview`. If you're setting up a new
machine, copy it in from your backup. **Never generate a new one**: a
different key can't update existing installs (§3.6 of
`docs/iterating-and-shipping.md`).

```bash
chmod 700 ~/keys && chmod 600 ~/keys/cedarview-release.jks
```

Keep it out of any plain-text backup (cloud sync, dotfile repos).

### What the first builds create

| Path | What | Size |
|---|---|---|
| `.qt/` (repo, gitignored) | Qt 6.11.1 Android kit, the matching host kit, and the aqtinstall venv. Fetched on the first Android build | ~2 GB |
| `build-android/` (repo, gitignored) | The Android build tree | ~500 MB |
| `~/.gradle/` | Gradle itself, the Android Gradle Plugin, and their caches | ~700 MB |
| `~/.android/debug.keystore` | The debug signing key, created by the script on the first default build | |
| `~/.android/analytics.settings`, `cache/` | Written by the Android Gradle Plugin and `sdkmanager` | |
| `~/.android/adbkey` | adb's private key, created the first time adb talks to a phone | |

`scripts/build-apk` starts from an empty environment (`env -i`; see
`docs/android.md` §2), keeping only `HOME`, `JAVA_HOME`, `ANDROID_SDK_ROOT`,
`ANDROID_NDK_ROOT`, the signing variables and `ANDROID_SERIAL`. So
`GRADLE_USER_HOME` and `ANDROID_USER_HOME` are ignored, and those two folders
always land in `~`. (Even overriding `HOME` doesn't move `~/.gradle`: Java takes
the home directory from the user database, not from `$HOME`.)

**If you back up your home folder, exclude all of these, plus the SDK in
`~/.local/share/android`.** Everything except the debug keystore can be
re-downloaded, and `~/.android` holds private keys. Together that's about
5.5 GB and some 50,000 files.

---

## 2. Demo: the app on sample data

### On the desktop (the development loop)

```bash
cmake -B build -G Ninja                 # first time only
cmake --build build && ./build/cedarview --demo
```

`--demo` starts the whole app on the fixtures in `tests/fixtures/`, built into
the binary. It needs no network, no sign-in and no browser. Other useful forms:

```bash
./build/cedarview --demo -v             # with debug logging
./build/cedarview --shoot shots/        # every screen, both themes, two widths, as PNGs, then exit
./build/cedarview --version
./build/cedarview                       # the real app: Microsoft sign-in, live data
ctest --test-dir build --output-on-failure   # all suites, offscreen, no network
```

QML is compiled into the binary, so a QML edit needs `cmake --build build`. It's
incremental and takes a few seconds. What the demo can't show you is listed in
`docs/iterating-and-shipping.md` §1.1.

**One difference from NixOS:** this build uses Arch's Qt (6.12 as of
2026-11-02). The APK still ships the pinned Qt **6.11.1** (`QT_VERSION` in
`scripts/build-apk`). The flake kept the two on the same release. Here they can
drift, so check anything Qt-version-sensitive on the phone before a release.

### On a phone

Install any build (§3) and, on the Welcome screen, tap **Look around with
sample data**. To leave: **Sign in** on the "Sample data" banner, or ⋮ →
**Sign in with Cedarville**. **Exit** also appears on the banner if this phone
has signed in before.

---

## 3. Building for Android

All commands run from the repo root. Every build goes through
`scripts/build-apk`, with two additions to the Nix form:

* `JAVA_HOME=/usr/lib/jvm/java-17-openjdk`, so Gradle gets Java 17.
* `CEDARVIEW_ANDROID_FHS=1`. The script refuses to run outside the Nix FHS
  shell, and this is the flag it checks for. Arch needs no FHS shell, so you
  set the flag yourself.

### Debug APK, for your own phone

Phone on USB with USB debugging on, and its RSA prompt accepted
(`docs/android.md` §1):

```bash
JAVA_HOME=/usr/lib/jvm/java-17-openjdk CEDARVIEW_ANDROID_FHS=1 ./scripts/build-apk --install
```

This writes `build-android/cedarview-<version>-arm64-v8a-debug.apk`, signed
with `~/.android/debug.keystore`. Each run installs over the last one. Leave out
`--install` to only build it. Add `--clean` only if the build itself breaks.
Watch it start:

```bash
adb logcat -c && adb logcat | grep -iE 'mycu|qml|cedarview'
```

### Signing variables, for the two release builds

Both release builds need the release key. Enter the passwords at a prompt so
they never land in your shell history or a file (zsh syntax):

```zsh
export QT_ANDROID_KEYSTORE_PATH=~/keys/cedarview-release.jks
export QT_ANDROID_KEYSTORE_ALIAS=cedarview
read -rs 'QT_ANDROID_KEYSTORE_STORE_PASS?Keystore password: '; echo
read -rs 'QT_ANDROID_KEYSTORE_KEY_PASS?Key password: '; echo
export QT_ANDROID_KEYSTORE_STORE_PASS QT_ANDROID_KEYSTORE_KEY_PASS
```

A keystore made by a recent `keytool` is PKCS12 underneath, whatever its
extension, and uses one password for both. They last until you close the
terminal.

**Before either release build, bump the version** in `CMakeLists.txt`
(`project(CedarView VERSION x.y.z …)`). The versionCode comes from it, and
Play and Android both refuse a version code they've already seen.

### Release APK, for GitHub Releases

```bash
JAVA_HOME=/usr/lib/jvm/java-17-openjdk CEDARVIEW_ANDROID_FHS=1 ./scripts/build-apk --release
gh release create v<version> cedarview-<version>-arm64-v8a.apk \
  --title "CedarView <version>" --notes "..."
```

This writes `cedarview-<version>-arm64-v8a.apk` to the repo root. The script
fails the build unless `apksigner` verifies it, every native library is 16
KB-aligned, and the manifest passes the checks in `docs/android.md`.

### App Bundle, for Google Play

```bash
JAVA_HOME=/usr/lib/jvm/java-17-openjdk CEDARVIEW_ANDROID_FHS=1 ./scripts/build-apk --aab
```

This writes two files to the repo root:

* `cedarview-<version>.aab`: **the file you upload** to Play Console.
* `cedarview-<version>.apks`: the split APKs Play would serve, for testing.

Before uploading, install it on your phone exactly as Play would:

```bash
bundletool install-apks --apks=cedarview-<version>.apks
```

The Play Console steps (app signing, privacy policy, Data safety, the 16 KB
check) are in `docs/iterating-and-shipping.md` §3.3.

**The first `.aab` you upload fixes your upload key for good.** Build that one
with the real key. To test the pipeline with a throwaway key, send the output
somewhere it can't be mistaken for a real upload:

```bash
keytool -genkeypair -keystore /tmp/throwaway.jks -storepass test123 -keypass test123 \
  -alias test -keyalg RSA -keysize 2048 -validity 1 -dname "CN=throwaway"
QT_ANDROID_KEYSTORE_PATH=/tmp/throwaway.jks QT_ANDROID_KEYSTORE_ALIAS=test \
QT_ANDROID_KEYSTORE_STORE_PASS=test123 QT_ANDROID_KEYSTORE_KEY_PASS=test123 \
JAVA_HOME=/usr/lib/jvm/java-17-openjdk CEDARVIEW_ANDROID_FHS=1 \
  ./scripts/build-apk --aab --out-dir /tmp/cedarview-throwaway
```

---

## 4. When something goes wrong

| Symptom | Cause / fix |
|---|---|
| `not inside the Android FHS shell` | `CEDARVIEW_ANDROID_FHS=1` is missing from the command |
| `the Android SDK at ~/.buildozer/... has no platforms/android-36` | `ANDROID_SDK_ROOT` isn't set in this terminal. Open a new one after editing `.zshenv` |
| `the NDK at ... is '...', but Qt 6.11.1 for Android is built with r27c` | Wrong NDK, or `ANDROID_NDK_ROOT` points somewhere else. Install `ndk;27.2.12479018` |
| Gradle or `androiddeployqt` complains about the Java version | It ran on the system Java (27). `JAVA_HOME=/usr/lib/jvm/java-17-openjdk` is missing from the command |
| `Qt6QmlAssetDownloaderPrivate could not be found because dependency Qt6TaskTree could not be found` during "Configuring" | Harmless. The app doesn't use that module, and the build carries on |
| A Qt host tool dies with `exit status 127` | A shared library is missing. Run the tool by hand to see which one (for example `.qt/6.11.1/gcc_64/libexec/qmlimportscanner --help`), then install it with pacman |
| `--aab needs bundletool on PATH` | `paru -S bundletool` |
| `INSTALL_FAILED_UPDATE_INCOMPATIBLE` | The phone has a build signed with a different key (a debug build from another machine, or a release build). Uninstall it first. That wipes the sign-in |
| `--release needs $QT_ANDROID_KEYSTORE_...` | The signing variables aren't exported in this terminal |
