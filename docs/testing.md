# Testing

```bash
nix develop
pytest                                  # 215 tests, ~2s, no network, no display
python scripts/smoke-transport          # real QtWebEngine, loopback server
python -m mycu --demo                   # the whole app against fixtures
python scripts/check-live               # one real authenticated request
```

## The two guarantees

**No test can reach the network.** `tests/conftest.py` monkeypatches every
socket entry point to raise. A test that quietly hit
`selfservice.cedarville.edu` would pass on your laptop, fail everywhere else,
and — worse — make the suite's result depend on whether you happen to be logged
in. Opt out with `@pytest.mark.allow_network`; nothing currently does, and
`scripts/check-live` is a script precisely so it does not have to.

**Qt never needs a display.** `QT_QPA_PLATFORM=offscreen` is set before PySide6
can be imported, so the Qt-touching tests run over SSH and in a build sandbox.

## What each file covers

| File | Covers |
|---|---|
| `test_chapel_provider.py` | Both parser branches, date formats, status vocabulary, dispatch, and that a login page raises `SessionExpired` rather than `ParseError` |
| `test_transport.py` | URL resolution, expiry detection both ways, `Response`, `FixtureTransport` |
| `test_session.py` | Persistence, 0600 permissions, corrupt/old files, platform paths |
| `test_login_flow.py` | The whole auth state machine, driven by URLs alone |
| `test_viewmodels.py` | List-model roles, error translation, the `-1` sentinel, re-entrancy |
| `test_platform_selection.py` | Backend choice, lazy imports, and the QML surface contract |
| `test_core_is_qt_free.py` | The architectural rule, by AST **and** by re-importing with PySide6 blocked at the meta-path |

## The tests that exist because something went wrong

Worth keeping, and worth understanding before you "simplify" the code they
guard:

- `test_json_camel_reads_the_student_name_not_the_term` — a substring match on
  `"name"` hit `termName` and put the term where the student's name goes. Exact
  matches now win globally before any fuzzy matching starts.
- `test_json_camel_does_not_mistake_allowed_for_used` — `"skips"` substring-
  matched `allowedSkips`, reporting 6 of 6 skips used to someone who had used 3.
  The generic spellings are deliberately absent from the candidate lists.
- `test_html_reads_the_official_totals` — a symmetric text window around "used"
  found the 6 from the *previous sentence* before the 3 that belonged to it.
  `_scan_number` now looks forward first.
- `test_sign_out_only_completes_once_we_are_back_on_selfservice` — the logout
  URL contains `post_logout_redirect_uri`, so matching on `"post_logout"`
  declared victory the instant we navigated.
- `test_unknown_status_does_not_count_as_a_skip` — guessing high would make the
  app frighten you over a status nobody has seen before.

## Manual checks that need a session

Once discovery is done:

1. `python -m mycu` → sign in → attendance renders.
2. Relaunch → no login prompt (the profile persisted).
3. Overflow menu → Sign out → login surface reappears.
4. Leave it until Entra expires the session, then refresh → it re-authenticates
   by itself and the data arrives.

## On Android

```bash
adb logcat -c && adb logcat | grep -iE 'python|qml|mycu'
python scripts/smoke-transport --android
```

`mycu/ui/app.py` logs to stdout deliberately; python-for-android tags stdout
with the app name, which makes it greppable.
