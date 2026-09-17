# The six data points: where each one lives, and what's still needed

Reconnaissance done 2026-09-16/17. Everything marked **verified** was checked
against the live service; everything else is explicitly flagged as unknown.

The headline: **these six things live on four or five different systems, and
only one of them is on `selfservice.cedarville.edu`.** That is the single most
important fact for planning, and it is why the transport now routes by origin.

| # | Want | Lives on | Auth | Status |
|---|---|---|---|---|
| 2 | Home Cooking, every meal | `diningdata.cedarville.edu` | **none** | ✅ **done, live** |
| 4 | Next chapel speaker | `mediaserve.cedarville.edu` | **none** | ✅ **done, live** |
| 3 | Chapel skips remaining | `selfservice.cedarville.edu` | SAML / Entra | ✅ **done, real API** |
| 1 | Flex dollar balance | `selfservice.cedarville.edu/Cedarinfo/Meals` | same Cedarville sign-in | ⏳ one more capture |
| 6 | Meals left this week | same page as #1 | same | ⏳ one more capture |
| 5 | Print quota balance | `printing.cedarville.edu` (PaperCut) | **separate** login | ⏸️ deferred by choice |

---

## ✅ 2. Home Cooking for every meal — DONE

Fully working, live, committed, and tested against a real capture.

```
GET https://diningdata.cedarville.edu/api/menus?days=N
```

**Unauthenticated.** The dining site's own script fetches it with
`credentials: "omit"`, so there is no session to have. Verified `days=1`, `7`
and `14`; returns exactly N dates starting today.

```json
{"2026-09-16": [{"venue": "Home Cooking", "meal": "Breakfast",
                 "slot": "breakfast",
                 "items": [{"name": "Chorizo Sausage Patties", "allergens": []},
                           {"name": "Shredded Cheese",
                            "allergens": [{"url": "…/hasDairy.png", "alt": "dairy"}]}]}]}
```

~20 blocks/day. `venue` is the station — Home Cooking, Grille, Italian,
Habanero, SubZone, Salad & Soup, Bake Shoppe, Allergen Aware, Garden Bites,
Power Bar, Self Cook, Breakfast All Day.

Two things the real data taught us that a reasonable person would have got
wrong:

- **`slot` is the sitting, `meal` is not.** `slot` is always one of
  `breakfast`/`lunch`/`dinner`/`anytime`. `meal` is free text: sometimes the
  sitting, sometimes `null`, sometimes a sub-station (`"Deli"`, `"yogurt bar"`).
  Sorting on `meal` put a yogurt bar after dinner. Ordering keys on `slot`.
- **The upstream sends `allergens`, but the site's own `menu.js` still reads
  `tags`.** Their renderer is out of date with their own backend. The provider
  accepts both.

The provider never sends `refresh=1` — the site uses it hourly to force an
upstream refetch from Pioneer College Caterers (`my.pcconline.com`), and a
personal app has no business making someone else's server work harder.

## ✅ 3. Chapel skips — DONE, against the real API

`/cedarinfo/chapelskip` 302s to `/CedarInfo/chapelskip/StudentDashboard`, which
is a **Vue 3 app**. The data is in three JSON endpoints that appear nowhere in
the served HTML:

```
GET /CedarInfo/ChapelSkip/GetStudentSummaryJson?studentId=<id>
GET /CedarInfo/ChapelSkip/GetStudentLedgerJson?studentId=<id>
GET /CedarInfo/ChapelSkip/GetStudentFinesJson?studentId=<id>
```

The `<id>` comes from an inline `const studentId = '…'` the page uses to
bootstrap itself, so the provider fetches the dashboard once per session and
reads it out. Plain GETs — the page's `__RequestVerificationToken` is for
*removing* entries, not reading them.

```json
{"Term": "2026FA", "TermName": "Fall Semester 2026",
 "SkipsUsed": 2, "SkipsTotal": 18, "SkipsRemaining": 16,
 "AllowanceBreakdown": [{"Reason": "Skips Allowed", "Count": 17, "Description": "Base semester allowance"},
                        {"Reason": "Manual Arrangement", "Count": 1, "Description": "For manual arrangement reasons"}],
 "RequirementReasons": ["Not a Distance Learner", "Registered for 15.5 credits (more than 6)", "Undergraduate Student"],
 "IsRequiredToAttend": true, "IsInGoodStanding": true, "Status": "good"}
```

### The thing that would have been got wrong

The ledger is **a ledger, not an attendance register**:

```json
[{"Count": 1,  "ChapelDate": "2026-08-20T10:00:00", "EntryType": "Chapel Skip",
  "CreatedReason": "Absent from Chapel 8/20/2026", "CanRemove": false},
 {"Count": -1, "ChapelDate": null, "EntryType": "Manual Adjustment",
  "CreatedReason": "Had ID replaced", "CanRemove": true}]
```

Those counts sum to **1**. The server reports `SkipsUsed: 2`. Both are correct
on the server's terms — the `-1` adjustment is *also* expressed as the `+1`
"Manual Arrangement" allowance line (17 + 1 = 18 total, 18 − 2 = 16 remaining).
**Recomputing from the ledger gives the wrong number.** The provider passes the
reported figures straight through, and a test asserts it.

Also: `ChapelDate` is `null` for adjustments — they are not tied to a chapel —
so the UI shows the reason instead of a date for those rows.

The earlier guess-driven module (tolerant key matching, an HTML table branch,
an `AttendanceStatus` vocabulary) is deleted. There is no per-session status
anywhere in the real data.

---

## ⏳ 1 & 6. Flex dollars and meals left — on Self-Service after all

**Transact is not needed.** These live at

```
https://selfservice.cedarville.edu/Cedarinfo/Meals
```

— the same origin and the same Microsoft sign-in as chapel. That is much better
news than the Transact route: no second credential, no second cookie jar, and
the existing WebView transport reaches it unchanged.

**Still needed: one more capture.** The first run traced no XHR for that page,
which means it is server-rendered — and `scripts/discover` had a bug that
skipped probing the current page whenever the trace found *anything* (here, a
tracking pixel). So the page's own HTML was never saved. That bug is fixed: the
current page is now always probed first.

```bash
python scripts/discover meals
```

## ⏸️ 5. Print quota — PaperCut, deferred

Found: `https://printing.cedarville.edu/user` →
`<title>PaperCut Login for Cedarville University</title>`. Note it resolves to
**the same IP as `selfservice.cedarville.edu`** (163.11.75.167), but that is
shared hosting, not shared auth.

The login is a plain `inputUsername` / `inputPassword` form posting to `/app`,
with a `jsessionid`. No SAML — it is a genuinely separate credential.

**Does it interfere with the Cedarville sign-in? No.** Different origin,
different cookie, no shared state; the Entra session is untouched either way.
The real cost is different: it is the only part of the app that would ask you to
type a password *into the app's own window* rather than onto Microsoft's page.

Deferred by choice — you called it the least important, and it needs another
capture anyway (the balance is in the `/app?service=page/UserSummary` HTML; the
first run only traced the balance-history PNG). If you want it later:

```bash
python scripts/discover papercut
```

Nothing else in the app changes to accommodate it.

## ✅ 4. Next chapel speaker — DONE

Found by `scripts/discover chapel-schedule`, which read the page's own
resource-timing log. Guessing had failed: `cedarville.edu/chapel` renders its
Upcoming tab client-side, so the schedule is nowhere in the served HTML, and
probing `/upcoming`, `/schedule`, `?future=1` on the old `/ChapelMedia/` path
all 404'd.

The page actually calls a **v2 API** nobody would have guessed the shape of:

```
GET https://mediaserve.cedarville.edu/ChapelMedia/api/v2/chapels/upcoming?page=1&count=N
```

**Unauthenticated.** 51 entries available when checked.

```json
{"TotalCount": 51, "Page": 1, "RetrievedCount": 20,
 "Items": [{"Id": "ICiRTos5BUiwi_xQP8LHyg",
            "Title": "Garrett Higbee",
            "Date": "2026-09-17T14:00:00Z",
            "Description": "…",
            "YouTubeId": "ySIBhMll7k0",
            "Speakers": ["Garrett Higbee"],
            "WillLiveStream": true}]}
```

Sibling endpoints on the same API: `/chapels/recent`, `/chapels/popular`,
`/chapel/live`.

Two things the real data teaches, both handled:

- **`Speakers` is often empty.** "Worship Chapel" and "SGA" are real scheduled
  chapels with no named speaker. Anything assuming `Speakers[0]` exists crashes
  on the *second* item in the live feed.
- **`Title` is usually the speaker's name verbatim**, so rendering
  "title — speaker" gives "Garrett Kell — Garrett Kell".

`Date` is ISO 8601 UTC (14:00Z = 10:00 Eastern); it is converted to local time
rather than assuming the phone is in Ohio. "Next" means the soonest chapel
*still in the future* — the feed keeps today's listed after it has started.

---

# The fast way: `scripts/discover`

Rather than asking you to read HAR files, there is now a tool that does it.

```bash
nix develop
python scripts/discover chapel            # Microsoft sign-in
python scripts/discover transact          # flex dollars + meals
python scripts/discover papercut          # print quota
python scripts/discover chapel-schedule   # NO LOGIN — hunts for the Upcoming XHR
```

It opens a real browser window on your screen. You sign in normally, navigate to
the page that shows the number you care about, and press **Capture**. It then:

1. Reads the page's own network log
   (`performance.getEntriesByType('resource')`) and lists **every URL the page
   fetched** — so an XHR that fills a tab shows up even when it is nowhere in
   the served HTML. This is how to find a JSON endpoint without reading anyone's
   JavaScript.
2. Fetches each of those endpoints through the logged-in session.
3. Prints the **structure**: JSON key names and types, or HTML table headers,
   row counts, the distinct values in each column, and every labelled number.

**Values are redacted by default.** The report shows `<int>`, `<str len=9>`,
`"2026-09-16" <- date-ish` — everything needed to write a parser, without your
student ID, name or balances. Dates and short enumerated strings are shown
verbatim because those *are* the structure.

The full raw body is written to `docs/captures/` (gitignored) so you keep it for
fixtures. Pass `--show-values` if you want real values in the report too.

Your password is never touched — you type it into the site's own page inside the
window, and the script only ever sees which URL you landed on and what came
back.

---

# What I need from you

If you run `scripts/discover` for each target and paste me the reports, that
answers almost everything below. These are the questions it *cannot* answer.

### A. Chapel (unblocks #3) — the original M0 task
1. Is `/cedarinfo/chapelskip` JSON or HTML? If JSON, **one record verbatim with
   values scrubbed** — I need key names only, values can all be `"x"`.
2. The exact status strings: "Absent" vs "A" vs "Unexcused"?
3. Are skips-allowed / used / remaining in the same payload, and what are those
   keys called?

Easiest path for all three: `nix develop && python scripts/check-live`.

### B. Next chapel speaker (unblocks #4) — I have no lead
4. **Where do you personally look this up?** A URL, an app name, a screenshot,
   anything. Options I'd guess at: the CU Chapel Plus app, a printed/emailed
   schedule, a page inside Self-Service, or the `#upcoming-chapels` tab on
   `cedarville.edu/chapel`.
5. If it's that tab: open `cedarville.edu/chapel`, click **Upcoming**, and in
   devtools → Network look for the XHR that fills it. Send me the URL.

### C. Flex dollars + meals (unblocks #1 and #6)
6. Confirm `cedarville.campuscardcenter.com` is where you check your balance —
   or tell me what you actually use. (A **GET Mobile** / **Transact eAccounts**
   phone app would be a much better integration target; do you have one?)
7. Log in there, then devtools → Network → reload → **Save All As HAR**, and
   tell me: is the balance in an XHR/JSON response (which URL?) or in the page
   HTML? Same question for meal swipes.
8. Are "flex dollars" and "meals left this week" on the same page, or two
   different screens?
9. **Decision I need from you:** is it acceptable for the app to show a
   Transact login inside its WebView and rely on the session cookie? That keeps
   your password out of the app but means logging in again when it lapses. The
   alternative is storing a credential, which I'd rather avoid.

### D. Print quota (unblocks #5)
10. Log in at `https://printing.cedarville.edu/user` and tell me what the
    balance page URL is once you're in (PaperCut's is usually something like
    `/app?service=page/UserSummary`).
11. Is there an SSO / "sign in with your Cedarville account" option on that
    page, or only the username+password box?
12. Same HAR question: is the balance in JSON or in the page HTML?

### E. One general decision
13. Four separate logins is a lot of friction. Do you want the app to prompt for
    each one **lazily** — only when you open that tab — rather than making you
    sign into everything at launch? That's what I'd recommend, and it's cheap to
    build now and expensive to retrofit.

**Safety note for all HAR captures:** they contain live session cookies, your
student ID and your name. `docs/captures/` and `*.har` are already gitignored.
Scrub before pasting anything to me — I only ever need *key names and
structure*, never values.
