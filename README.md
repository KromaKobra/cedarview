# myCU

A personal Android + desktop app for reading **your own** Cedarville student
records. Chapel attendance first; grades, schedule and student account are each
one more provider module.

Python + PySide6 + QML, packaged with Nix.

---

## Status

| | |
|---|---|
| Core, transport, login, viewmodels, QML | **written, 244 tests passing** |
| WebView transport, desktop | **verified end to end** — `scripts/smoke-transport`, 7/7 |
| **Dining menus (Home Cooking)** | ✅ **working against the live API**, real fixture committed |
| **Next chapel speaker** | ✅ **working against the live API**, real fixture committed |
| **Chapel skips** | ✅ **working against the real API**, real scrubbed fixtures |
| Live authenticated request | ✅ made — three JSON endpoints found and parsed |
| **Meals left + dollar balances** | ✅ **working against the real page**, real scrubbed fixture |
| **Android** | ✅ **built, installed and running** on a moto g power 5G (2024), arm64-v8a / Android 15. Dining and next-chapel show live data on the phone; `scripts/build-apk` drives the whole build |

**`docs/data-sources.md` is the one to read**: where each of the six data points
actually lives, what's verified, and the numbered list of things only you can
answer. Then `docs/next-steps.md` for ordering.

## Try it right now

No login, no network, no phone:

```bash
nix develop
python -m mycu --demo
```

That runs the whole app — QML, viewmodels, list model, the lot — against
`tests/fixtures/`. It is how you develop the UI without a session.

```bash
pytest                              # 244 tests, no network, no display needed
python scripts/smoke-transport      # proves the WebView transport works
```

## The real thing

```bash
nix develop
python -m mycu
```

Opens a window, sends you to Microsoft's sign-in page if needed, and shows your
chapel skips, meal balances and this week's menus.

The parser is **no longer a guess** — `scripts/discover` signed in and captured
every endpoint, and each provider is written and tested against a real scrubbed
capture. What has not been done is driving the *assembled app* through a live
sign-in, on either platform. That is step 1 and 2 of `docs/next-steps.md`, and
it needs you.

## How it works, briefly

`selfservice.cedarville.edu` is a SAML 2.0 Service Provider federated to Entra
ID. There is no OAuth client to register, so login happens interactively in an
embedded WebView — which means **the app never sees your password**.

The session cookie is `HttpOnly` and, on Android, unreadable by Qt. So instead
of harvesting it, the app lets the browser make the request from inside the
logged-in page and passes the body back through `runJavaScript`. That uses only
APIs present on both QtWebEngine and QtWebView, which is what makes the Android
port a packaging exercise rather than a rewrite.

Session expiry is *detected, not predicted*: if a request lands on an identity
provider, the session is dead and the login surface reopens. Nothing tracks
cookie lifetimes.

Full reasoning in `docs/architecture.md`.

## Layout

```
mycu/core/        pure Python — no Qt, testable offline
mycu/ui/          Qt: transport, login, viewmodels, QML
mycu/platform/    the only Android-specific code
tests/            flat pytest; the suite cannot open a socket
scripts/          smoke-transport, check-live, build-apk
docs/             discovery (M0), architecture, android, next-steps
```

## Docs

| File | Read it when |
|---|---|
| **`docs/android-status.md`** | **first, if you are picking Android back up** — what is done, how the build works, what is left |
| `docs/data-sources.md` | the six data points, and what I need from you |
| `docs/next-steps.md` | right after |
| `docs/discovery.md` | before touching the parser |
| `docs/architecture.md` | before changing the transport or adding a provider |
| `docs/android.md` | the toolchain reference, and when the APK misbehaves |
| `tests/fixtures/README.md` | when replacing the synthetic fixtures |

## Operating notes

This reads your own records from your own account — ordinary use of the portal.
Kept that way deliberately: cache and refresh on demand rather than polling, an
honest User-Agent, and session state in app-private storage (`filesDir` on
Android, `0600` under `$XDG_DATA_HOME` on desktop), never in git. The app never
sees or stores your password.
