# M0 — Discovery

> **Historical, and done.** Discovery settled every question below; the
> answers are in `docs/data-sources.md`. This was written when the app was
> Python, so the file paths in the "not confirmed" list name the Python modules
> of the time — the code is now in `src/`. The procedure at the end
> ("Promoting the capture into a fixture") is still how a new capture becomes a
> fixture, and is kept current.

**This is the only milestone that cannot be done for you**, because it needs an
authenticated session. Everything in the repo downstream of it is written,
tested and waiting; it is all currently validated against *synthetic* fixtures,
which means the parser handles a plausible chapel page but nobody knows yet
whether it handles the real one.

Budget: about twenty minutes.

---

## What is already confirmed

Verified live, unauthenticated, on 2026-09-16:

```
GET https://selfservice.cedarville.edu/CedarInfo/ChapelAttendance
  → 302  https://selfservice.cedarville.edu/cedarinfo/chapelskip
  → 302  https://login.microsoftonline.com/81c32413-015d-4ba8-a93b-e1c28e355738/saml2
           ?SAMLRequest=…&RelayState=%2Fcedarinfo%2Fchapelskip
```

So: one host, one SAML session, and `RelayState` carries the return path. That
is the entire basis of the login design in `src/ui/login.cpp`, and it holds.

## What is *not* confirmed

1. **Does `/cedarinfo/chapelskip` serve JSON or a server-rendered Razor page?**
   Ellucian's stock `/Student/…` areas are XHR/JSON-heavy, but `/cedarinfo/` is
   Cedarville-custom and that tells us nothing. The app handles both — see
   `mycu/core/providers/chapel.py` — but only one of those code paths is real.
2. **If JSON: what are the keys called?** Every candidate name in
   `chapel.py` is a guess. They are all marked:
   ```
   grep -rn 'M0:' mycu/
   ```
3. **Which cookies does a request actually require?**
4. **Does an anti-forgery token (`__RequestVerificationToken`) gate anything?**
   If a hidden form field is required for the data request, the transport needs
   to read it out of the page first. Nothing else in the design changes.

---

## Doing the capture

1. Firefox → `https://selfservice.cedarville.edu/cedarinfo/chapelskip`, log in
   normally.
2. F12 → **Network** tab. Tick **Persist Logs** and **Disable Cache**.
3. Reload the page.
4. Right-click in the request list → **Save All As HAR**. Put it in
   `docs/captures/` (already `.gitignore`d, so it cannot be committed by
   accident — it contains live session cookies).

Then look for:

- **An XHR/fetch request returning JSON.** Filter the Network tab by `XHR`. If
  one exists, that URL is what the provider should request instead of the page
  itself — note it exactly, query string included.
- **If there is no XHR**, the data is in the page HTML. Right-click the
  `chapelskip` document → Copy → **Copy as HTML** (the *rendered* DOM, not
  "view source", if any of it is client-rendered).

---

## Capturing a page's network traffic (when a page changes)

When a myCU page is redesigned and a provider stops parsing it, as the Meals
page did in September 2026, record what the new page actually does rather
than guessing:

```bash
nix develop --command python scripts/discover meals
#   sign in → click through to the flex / meal-plan info → wait for the numbers → Capture
```

A recorder is injected into every page and frame before the page's own
scripts run. It logs each `fetch`, `XMLHttpRequest`, `sendBeacon` and form
submission, with its method, headers, body, status and response body, from
sign-in until you press Capture, across page changes. It writes to
`docs/captures/`:

| File | Contents | Share it? |
|---|---|---|
| `meals-report-<stamp>.txt` | RECORDED TRAFFIC: each request's method, URL, header names, and the *shape* of the request and response bodies. Values are redacted; cookies, tokens and bare-digit IDs are always masked. Responses mentioning flex/meal/dining are flagged `<<<`. | **Yes**, this is the one to hand over |
| `meals-network-<stamp>.har` | Every recorded request with full values. Opens in DevTools (Network → Import HAR). | **No**, it has live cookies and tokens |
| `meals-dom-<stamp>.html` | The rendered DOM as it was on screen | No, it has your name and balances |

Useful flags:

- `--start URL`: open somewhere else first, if the information moved. The
  report lists every navigation, so you will see where it ended up either way.
- `--devtools 9222`: open `http://127.0.0.1:9222` in Chromium to get the full
  Network panel on the discover window. Tick **Preserve log**. Use this for
  anything the in-page recorder cannot see: cross-origin iframes, service
  workers, or plain link navigations.
- `--from-har x.har`: no window. Summarise any HAR into the same redacted
  report: ours, or one saved from Chrome/Firefox (**Save all as HAR**),
  including one captured on another machine.
- `--all-hosts`: include the analytics and static-asset requests that are
  hidden by default.
- `--show-values`: put real values in the report. Credentials stay masked.

URLs already in RECORDED TRAFFIC are not re-probed with a bare GET afterwards.
Replaying a POST or a token-guarded endpoint that way only produces a
misleading error.

---

## Recording what you found

Fill this in and keep it in the repo — it is the answer sheet for everyone
reading the parser later, including you in six months.

```markdown
## Findings — <date>

Payload:         [ ] JSON at <url>      [ ] HTML in the page document
Final URL after the redirect chain: <url>
Required cookies: <names only — never values>
Anti-forgery token required: [ ] no  [ ] yes, as <field/header name>
Response Content-Type: <value>
Approximate size: <n> KB
```

If JSON, paste one record verbatim (values scrubbed) so the key names are on
the record:

```json
{ "<key>": "<value>", "…": "…" }
```

---

## Promoting the capture into a fixture

1. **Scrub it.** Student ID, name, email, username, every cookie value,
   `__RequestVerificationToken`, `SAMLResponse`, and any `localStorage` dump
   inlined in the page. Change *values*, never *keys or tags* — the structure is
   the thing being tested.
2. Save it over `tests/fixtures/cedarinfo_chapelskip.html`, or as
   `cedarinfo_chapelskip.json` if it is JSON. The `.json` extension wins if both
   exist, which is how the app switches payload types with no code change.
3. `ctest --test-dir build --output-on-failure` — the expected values in
   `tests/tst_chapel_provider.cpp` will now be wrong. **That is the useful
   part.** Each failure is a place your assumptions and reality diverged.
   Update the assertions to the truth.
4. Make `src/core/providers/chapel.cpp` match the real shape. Tolerant matching
   exists to survive an unknown; once it is known, precision is better.

## Then

```bash
./build/cedarview       # log in, see your own chapel record
```

That is M1 done, and it is genuinely useful on its own — before any Android
tooling exists. See `docs/next-steps.md` for what comes after.
