# Next steps

What is done, what is not, and exactly what only you can do.

---

## What actually works right now

Verified by running it, not by reading it:

- **225 tests pass** (`pytest`) — parsing, transport protocol, expiry detection,
  session persistence, the login state machine, viewmodels, the QML contract.
  No test can open a socket; `conftest.py` blocks it.
- **The app runs.** `python -m mycu --demo` loads the QML, populates the list
  model and reports "8 records, used=3 allowed=6" from fixtures.
- **The WebView transport works end to end.** `scripts/smoke-transport` drives a
  real QtWebEngine surface against a loopback server: 7/7, including a 300 KB
  body surviving `runJavaScript`, two concurrent requests not being confused,
  and a stalled request raising instead of hanging. This was the riskiest part
  of the design and it is no longer a guess.
- **The environment is confirmed.** Against your actual pin (`nixos-26.05`):
  PySide6 6.11.0, shiboken6 6.11.0, Qt 6.11.1, qtwebengine 6.11.1 with its QML
  module present, lxml 6.0.2.
- **The redirect chain is confirmed**, re-checked live:
  `/CedarInfo/ChapelAttendance` → 302 → `/cedarinfo/chapelskip` → 302 →
  `login.microsoftonline.com/81c32413-…/saml2?…&RelayState=%2Fcedarinfo%2Fchapelskip`.

- **The Android app runs on hardware.** Built, installed and launched on a
  moto g power 5G (2024) — arm64-v8a, Android 15. On the phone: QtWebView
  initialises, the QML loads, and the Dining and next-chapel views show live
  data fetched over HTTPS. `scripts/build-apk` does the whole thing.

## What has never been executed

- A completed interactive sign-in **on the phone**, and therefore the
  authenticated chapel-skip fetch on Android. Everything up to the Microsoft
  sign-in page is confirmed working there.
- `scripts/check-live` (it needs a login on the first line of work it does).

---

## 1. Discovery — blocking, ~20 minutes, only you can do it

**This is the one thing standing between the repo and a working app.**

The parser is written against **synthetic fixtures**. Nobody has seen the real
`/cedarinfo/chapelskip`. It may serve JSON or a server-rendered Razor page — the
app handles both, but only one of those code paths is real, and if it is JSON,
every field name in it is currently a guess.

Do this: **`docs/discovery.md`**. In short — log in in Firefox, devtools →
Network, reload, Save All As HAR, and note whether the data arrives as JSON
(from which URL?) or as HTML in the page.

Then either:

```bash
# the shortcut, if you'd rather let the app do the capture
nix develop
python scripts/check-live
```

It signs you in, fetches the page once, tells you whether it is JSON or HTML,
shows what the parser made of it, and writes the raw body to `docs/captures/`
(gitignored — it will contain your name and student ID).

**What I need from you if you want me to finish the parser**, in decreasing
order of usefulness:

1. The output of `scripts/check-live`, or the answers in
   `docs/discovery.md`'s findings block.
2. If JSON: **one record, verbatim, with values scrubbed** — I need the key
   names, e.g. `{"ChapelDate": "...", "AttendanceStatus": "..."}`. Keys only
   matter; the values can all be `"x"`.
3. If JSON: whether the totals (skips allowed / used / remaining) are in the
   same payload, and what those keys are called.
4. The exact status strings Cedarville uses — "Absent" vs "A" vs "Unexcused".
   `AttendanceStatus.parse` currently guesses at a dozen spellings.
5. Whether the request needs `X-Requested-With: XMLHttpRequest` to return data
   rather than a page. (`WebViewTransport.set_extra_headers` exists for this and
   is deliberately empty — setting it changes what ASP.NET returns, and guessing
   at two unknowns at once is how you lose an afternoon.)

Everything unconfirmed is marked in the source:

```bash
grep -rn 'M0:' mycu/          # guessed field names
grep -rn 'VERIFY:' mycu/      # things only a device can settle
```

## 2. Desktop app — ~1 hour after discovery

```bash
nix develop
python -m mycu
```

Then update `tests/fixtures/` with the scrubbed real capture and run `pytest`.
**The failures are the point** — each one marks a place where the guesses and
reality diverged. Fix the assertions to the truth, then trim `chapel.py`: delete
the branch you don't need and replace the candidate-key tuples with the real
names. Tolerant matching exists to survive this one unknown; once it is known,
precision is better.

Verification worth doing by hand:

- relaunch → the session persisted, no login prompt
- Sign out → login surface reappears
- let it sit until the session expires → it re-authenticates on the next refresh

**Ship it here.** A desktop app showing your chapel skips is useful on its own,
and it proves the parsing, the transport and the expiry handling before any
Android tooling exists.

## 3. Android toolchain — ~half a day, mostly waiting

Full detail in `docs/android.md`. The parts that are yours:

**a. NixOS config.** I did not touch `/etc/nixos` — you asked me to stay in this
folder, and it needs `sudo` anyway. Neither line exists there today:

```nix
programs.adb.enable = true;
users.users."kroma".extraGroups = [ "networkmanager" "wheel" "bluetooth" "adbusers" ];
```

(That second line appends to your current list — keep the three that are there.)

```bash
sudo nixos-rebuild switch --flake /etc/nixos#nixos
```

Then **log out and back in**. Group membership does not apply to your current
session, and this is the classic half-hour loss.

**b. Enable USB debugging on the phone**, plug it in, `adb devices`, accept the
RSA prompt.

**c. Build the stock PySide6 QML example to an APK first — not this app.**
`pyside6-android-deploy` is not in nixpkgs (confirmed: no `pyside6-*` tools on
PATH); it ships in the PyPI wheels and drives buildozer underneath, which wants
an FHS layout. `flake.nix` provides one:

```bash
nix run .#android-shell
```

If the toolchain fights NixOS, you want to find out against twenty lines of
hello-world, not while debugging the transport.

**Done, but not the way this predicted.** The chapel page did turn out to be
JSON — but the shortcut above was wrong about `lxml` disappearing with it,
because the **meal-plan page is server-rendered HTML** and inherited the
dependency. It was retired instead by replacing that one parser with
`mycu/core/minihtml.py` (~80 lines of stdlib `html.parser`). The runtime path
is now pure Python, so the APK needs no cross-compiled C extension, and
`tests/test_core_is_qt_free.py` fails if one is reintroduced.

## 4. Android port — wiring, once the toolchain works

`scripts/build-apk` drives it and checks preconditions properly. Make sure
**QtWebView** is in the bundled modules — without it there is no login and no
transport, and it is the most likely thing to be missing on the first attempt.

Then `adb install -r mycu.apk` and `adb logcat | grep -iE 'python|qml|mycu'`.

Three things are flagged `# VERIFY:` for the device:

| Where | What |
|---|---|
| `qml/WebSurfaceAndroid.qml` | The `WebView.LoadFailedStatus` enum spelling in the Qt 6.11 Android build. Most likely QML runtime error. |
| `platform/android.py` | Whether the federated logout actually drops the Self-Service cookie, not just the Entra one. |
| the transport | That a large body survives `runJavaScript` on QtWebView. Proven at 300 KB on QtWebEngine; the system WebView's marshalling limits are undocumented. If it truncates, chunk the read script. |

Run `python scripts/smoke-transport --android` on-device to settle all three at
once.

## 5. More providers

Grades, schedule, student account. One module each:

1. capture the page, 2. scrubbed fixture, 3. `providers/<name>.py`,
4. `tests/test_<name>_provider.py`, 5. viewmodel + QML page.

Steps 1–4 need no phone, no display and no network. The session and transport
are untouched.

---

## Two judgement calls I made, and why

**`mycu/platform/` instead of a top-level `platform/`.** The plan's tree put it
at the root, which would shadow the standard library's `platform` module for
anything on `sys.path` — and Qt, setuptools and python-for-android all import
it. The failure surfaces far from its cause and is miserable to debug inside a
buildozer run. Nesting it under `mycu` keeps `import platform` resolving to the
stdlib; `tests/test_core_is_qt_free.py` asserts the nesting stays.

**Both parser branches, not one.** The plan says if the page is HTML the only
change is `lxml` instead of `json.loads`. That's right, but since the answer is
unknown, writing both and sniffing at runtime means the app works either way on
the first authenticated run, and you delete half afterwards. The `.json` fixture
extension taking priority over `.html` means switching payload types needs no
code change at all.
