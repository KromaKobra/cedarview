# Fixtures

Every parsing test in this repo runs against a file in this directory. No test
touches the network (`conftest.py` blocks it outright) and none needs Qt.

## ⚠️ The files here are SYNTHETIC

Nothing in this directory came from Cedarville. They are hand-written stand-ins
built from the *documented* shape of ASP.NET / Ellucian output so that the
parser, the viewmodels and the UI could be written and tested before anyone had
an authenticated session.

They prove the parser handles *a* plausible page. They do **not** prove it
handles *the* page. Replacing them is milestone M0 and it is the single highest-
value thing you can do next — see `docs/discovery.md`.

| File | What it stands in for |
|---|---|
| `cedarinfo_chapelskip.html` | A server-rendered Razor attendance table. Served by `FixtureTransport` and therefore by `python -m mycu --demo`. |
| `samples/chapel_json_pascal.json` | JSON with PascalCase keys — the ASP.NET default. |
| `samples/chapel_json_camel.json` | JSON with camelCase keys and an ASP.NET wrapper object, exercising the nested-list search. |
| `samples/chapel_json_bare_list.json` | A bare top-level array, no envelope, no totals. |
| `samples/entra_login_page.html` | What comes back when the session has expired. Used to test that this is detected and never parsed. |

## Replacing them with real captures

1. Capture per `docs/discovery.md`.
2. **Scrub** before the file goes anywhere near git:
   - student ID / `PERSON.ID` / any seven-digit number
   - your name, email, and username
   - every cookie value, `__RequestVerificationToken`, and `SAMLResponse`
   - `sessionStorage` / `localStorage` dumps embedded in the page
3. Keep the structure intact — the point is to test the parser against the real
   markup, so change values, not tags or keys.
4. Drop the file in as `cedarinfo_chapelskip.html` (or `.json` — the `.json`
   extension wins if both exist, which is how you switch the app over to a JSON
   payload without touching code).
5. Run `pytest`. Update the expected values in `tests/test_chapel_provider.py`.

`.gitignore` already excludes `docs/captures/` and `*.har` so a raw, unscrubbed
capture cannot be committed by accident.
