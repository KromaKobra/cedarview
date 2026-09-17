# Android: toolchain and packaging

**Status: written, never executed.** No device has been connected to this
project. Everything below follows from verified facts about the environment
(nixpkgs contents, Qt documentation, what is and is not on `PATH`), but the
build itself has not been run. Treat it as a checklist to work through, not a
transcript of something that worked.

---

## M2 — Prove the toolchain with something that isn't your app

The order matters. Build **the stock PySide6 QML example** to an APK first. If
the toolchain fights NixOS, you find out against twenty lines instead of while
debugging your own transport.

### 1. NixOS: enable adb

Neither of these exists in `/etc/nixos/configuration.nix` today (checked). Add:

```nix
programs.adb.enable = true;                        # pulls in android-udev-rules
users.users."kroma".extraGroups = [ "networkmanager" "wheel" "bluetooth" "adbusers" ];
```

Note that the second line **appends to your existing list** — the current value
is `[ "networkmanager" "wheel" "bluetooth" ]`, so keep those.

```bash
sudo nixos-rebuild switch --flake /etc/nixos#nixos
```

Then **log out and back in.** Group membership does not apply to your current
session, and this is the single most common way to lose half an hour here.

Verify:

```bash
adb devices          # accept the RSA prompt on the phone
```

On the phone: Settings → About → tap Build Number seven times → Developer
options → USB debugging.

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

Inside it:

```bash
python -m venv .venv-android
source .venv-android/bin/activate
pip install pyside6==6.11.0 --no-cache-dir
```

That pulls ~1 GB of wheels. `BUILDOZER_HOME` is already pointed at
`.buildozer-home/` in the project so the SDK/NDK downloads land somewhere you
can `git clean`, not in `~`.

### 3. Hello world first

```bash
pyside6-android-deploy \
  --wheel-pyside <path to the PySide6 Android wheel> \
  --wheel-shiboken <path to the shiboken6 Android wheel> \
  --name hello \
  --init            # writes a pysidedeploy.spec you can inspect and re-run
```

Point it at a stock PySide6 QML example, build, then:

```bash
adb install -r hello.apk
adb logcat | grep -i python
```

**Only when that runs on the phone** should you point the tool at this repo.

---

## M3 — Port this app

### Modules to bundle

`pyside6-android-deploy` prunes aggressively by default. This app needs, beyond
the defaults:

- **`QtWebView`** — without it there is no login and no transport. This is the
  most likely thing to be missing on the first attempt; the symptom is
  `ImportError: PySide6.QtWebView` in logcat, which
  `mycu/platform/android.py` turns into an explicit message pointing here.
- `QtQuick`, `QtQuickControls2`, `QtQml`
- `QtNetwork`

Do **not** try to bundle `QtWebEngine`. It does not exist on Android.

### lxml

`lxml` is a C extension and python-for-android needs a recipe for it. Two paths:

1. Use the `lxml` recipe from python-for-android (it exists, but has historically
   needed libxml2/libxslt cross-compiled).
2. **Avoid the problem entirely** — if M0 shows the chapel page is JSON, `lxml`
   is not needed at all. Delete the HTML branch from `chapel.py`, drop `lxml`
   from `pyproject.toml`, and the APK gets simpler and smaller.

This is a concrete reason to do M0 before M2.

### Permissions

`INTERNET` only. The app reads your own records from your own account; it needs
nothing else. Do not let the packaging tool add more by default — check the
generated `AndroidManifest.xml`.

### Signing

Generate a release keystore locally:

```bash
keytool -genkey -v -keystore mycu-release.keystore \
        -alias mycu -keyalg RSA -keysize 4096 -validity 10000
```

`.gitignore` already excludes `*.keystore`, `*.jks` and `keystore.properties`.

**Back it up somewhere outside this repo.** It is not recoverable, and losing it
means you can never ship an upgrade to an already-installed build — only an
uninstall/reinstall, which wipes the session.

---

## Debugging on-device

```bash
adb logcat -c                     # clear first, or you drown
adb logcat | grep -iE 'python|qml|mycu'
```

`mycu/ui/app.py` logs to **stdout** deliberately — python-for-android tags
stdout with the app name, which makes it greppable.

### Things flagged for on-device verification

Each is marked `# VERIFY:` in the source:

| Where | What to check |
|---|---|
| `qml/WebSurfaceAndroid.qml` | The `loadingChanged` handler's `WebView.LoadFailedStatus` enum spelling against the Qt 6.11 Android build. Most likely place for a QML runtime error. |
| `platform/android.py` | Whether the federated logout round trip really drops the Self-Service cookie, not just the Entra one. |
| transport, end to end | That a large body survives `runJavaScript` on QtWebView. It was verified at 287 KB on QtWebEngine; the marshalling limits on the system WebView are not documented. If it truncates, the fix is to chunk the body in the read script. |

### The optional JNI upgrade

If `QJniObject` turns out to exist in the Android PySide6 build, then
`android.webkit.CookieManager.getInstance().getCookie(url)` yields the real
cookies and the WebView transport can be swapped for a plain `httpx` client —
faster, cleaner, better error handling.

It was **not** found in the desktop x86_64 build, but Qt compiles its JNI
classes only on Android, so that proves nothing either way. Check on-device with:

```python
from PySide6 import QtCore
print([n for n in dir(QtCore) if 'Jni' in n])
```

Strictly an optimisation. Only attempt it once the APK ships.
