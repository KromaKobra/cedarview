# The six data points: where each one lives, and what's still needed

Reconnaissance done 2026-09-16/17. Everything marked **verified** was checked
against the live service; everything else is explicitly flagged as unknown.

The headline: **the six things live on three systems, two of which need no login at
all.** That is the single most
important fact for planning, and it is why the transport now routes by origin.

| # | Want | Lives on | Auth | Status |
|---|---|---|---|---|
| 2 | Home Cooking, every meal | `diningdata.cedarville.edu` | **none** | ✅ **done, live** |
| 4 | Next chapel speaker | `mediaserve.cedarville.edu` | **none** | ✅ **done, live** |
| 3 | Chapel skips remaining | `selfservice.cedarville.edu` | SAML / Entra | ✅ **done, real API** |
| 1 | Flex dollar balance | `selfservice.cedarville.edu/CedarInfo/Meals/GetBalanceJson` | same Cedarville sign-in | ✅ **done, real endpoint** |
| 6 | Meals left this week | same page as #1 | same | ✅ **done, real page** |

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

## ✅ 1 & 6. Flex dollars and meals left — DONE (re-done 2026-09-22)

**Transact is not needed.** These live on Self-Service, same origin and same
Microsoft sign-in as chapel, with no second credential.

In September 2026 the page was rebuilt as a Vue app, which broke the original
prose scraper. Recaptured with `scripts/discover meals`, the page now works in
two steps, and `MealsProvider.fetch` does the same:

```
GET /Cedarinfo/Meals
    → HTML with no figures, only who to look up:
      <cu-container id="app" data-target-id="0000000" data-target-card="" …>

GET /CedarInfo/Meals/GetBalanceJson?id=0000000      (plain GET, no token or header)
    → {"Status": "ok", "Found": true, "PlanName": "21 Meals",
       "Balances": [
         {"Name": "Board Meals",   "Type": "MEAL",     "Amount": 16,     "IsCurrency": false},
         {"Name": "Flex Dollars",  "Type": "DEBIT",    "Amount": 102.34, "IsCurrency": true},
         {"Name": "Meal Exchange", "Type": "EXCHANGE", "Amount": 16,     "IsCurrency": false}],
       "RecentTransactions": [{"Date", "Activity", "MealPeriod", "Amount", "IsDeposit"}, …]}
```

`Status` can also be `"error"` (with a `Message`) or `"no_card"`, and
`Found: false` means no plan on file. Those last two render as blanks rather
than as errors.

### Which dollars are which

There are still two kinds of dollars, and they must not be conflated:

- **"Flex Dollars"** on the new page is what the old page called **"Meal Plan
  Dining Dollars"**: part of the plan, and they expire at term end. The proof
  is the same account five days apart: $112.08 before, $102.34 after two flex
  purchases of exactly $3.74 + $6.00. It is `MealPlan.dining_dollars`, shown
  as *Temporary Flex*.
- **Voluntary Flex Dollars** (purchased, do not expire) are `flex_dollars`,
  shown as *Permanent Flex*. The captured response had **no** such tender (the
  old page showed $0.00). A tender is only treated as voluntary when its name
  says so, so Permanent Flex shows "—", not a guessed $0.00.

Tenders are matched by `Type` and name, never by position. *Meal Exchange* is
parsed past but not shown.

### The plan's name

The endpoint now gives it (`PlanName`: "21 Meals", "Block 120"), so the summary
card shows "21 Meals per week". The cycle is still read from the name rather
than assumed: "N Meals" is weekly and "Block …" is per term. Any other name is
shown as is, with no "this week" qualifier.

## ✂️ 5. Print quota — dropped

Removed from scope at your request, and removed from the code: there is no
PaperCut target in `scripts/discover` and no provider for it.

Recorded for whoever wants it later: the balance is on
`printing.cedarville.edu/app?service=page/UserSummary`, behind a plain
`inputUsername` / `inputPassword` form — a genuinely separate credential, not
your Microsoft account. It does **not** interfere with the Cedarville session
(different origin, different cookie, no shared state); the reason to leave it
out is that it would be the only part of the app asking you to type a password
into the app's own window rather than onto Microsoft's page.

---

# The fast way: `scripts/discover`

Rather than asking you to read HAR files, there is now a tool that does it.

```bash
nix develop
python scripts/discover chapel            # Microsoft sign-in
python scripts/discover meals             # flex dollars + meals left
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
