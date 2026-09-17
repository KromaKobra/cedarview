# M0 — Discovery

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
is the entire basis of the login design in `mycu/ui/login.py`, and it holds.

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
3. `pytest` — the expected values in `tests/test_chapel_provider.py` will now be
   wrong. **That is the useful part.** Each failure is a place your assumptions
   and reality diverged. Update the assertions to the truth.
4. Trim `chapel.py` to match: delete the branch you do not need, and replace the
   candidate-key tuples with the real names. The tolerant matching exists to
   survive this one unknown; once it is known, precision is better.

## Then

```bash
python -m mycu          # log in, see your own chapel record
```

That is M1 done, and it is genuinely useful on its own — before any Android
tooling exists. See `docs/next-steps.md` for what comes after.
