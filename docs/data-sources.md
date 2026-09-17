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
| 3 | Chapel skips remaining | `selfservice.cedarville.edu` | SAML / Entra | ⚠️ written, parser unverified |
| 1 | Flex dollar balance | `cedarville.campuscardcenter.com` (Transact) | separate username+password | ❌ blocked — needs you |
| 6 | Meals left this week | same as #1, probably | same | ❌ blocked — needs you |
| 5 | Print quota balance | `printing.cedarville.edu` (PaperCut) | separate username+password | ❌ blocked — needs you |

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

## ⚠️ 3. Chapel skips remaining — written, unverified

Unchanged from before: the redirect chain is verified, the parser is not.
`/cedarinfo/chapelskip` may serve JSON or Razor HTML and the app handles both.
See `docs/discovery.md` — this is still the blocking task for chapel.

---

## ❌ 1 & 6. Flex dollars and meals left — Transact

Found: `https://cedarville.campuscardcenter.com/ch/login.html` →
`<title>Transact Cardholder Website</title>` (formerly Blackboard Transact).
This is where a campus card's flex/declining balance and meal-plan swipes live.

**The problem:** the login page is a plain `username` + `password` form with an
`__ncforminfo` anti-CSRF token. The four "Single Sign On Display" markers in the
page are **empty template placeholders** — SSO is not enabled on Cedarville's
instance. So unlike Self-Service, there is no federated login we can hand off to
Microsoft.

That matters because it breaks the property the app has had so far: *your
password is never seen or stored*. To read Transact, something would have to
collect a credential.

**Options, in the order I'd pick them:**

1. **Type it into the embedded WebView yourself, per session.** The app never
   stores it; the WebView keeps a cookie like any browser. Same transport as
   Self-Service, just aimed at a second origin. Keeps the no-stored-password
   property. Costs you a login when the session lapses.
2. **Check for a mobile-app API first.** Transact's *eAccounts* / GET Mobile
   apps use a documented-ish JSON API. If Cedarville is on it, that is a much
   nicer integration than scraping a 2009-era ASP page. **I cannot check this
   without logging in.**
3. Store the credential in the OS keychain. Possible; I would rather not.

**What I need from you:** see the checklist at the bottom.

## ❌ 5. Print quota — PaperCut

Found: `https://printing.cedarville.edu/user` →
`<title>PaperCut Login for Cedarville University</title>`. Note it resolves to
**the same IP as `selfservice.cedarville.edu`** (163.11.75.167), but that is
shared hosting, not shared auth.

Same problem as Transact: the login is a plain `inputUsername` / `inputPassword`
form posting to `/app`, with a `jsessionid`. No SAML redirect at the entry
point.

PaperCut's user portal shows the balance on its summary page, and PaperCut
installs commonly expose `/rpc/api/xmlrpc` — but that is admin-authenticated and
not something a student account can or should use. The realistic route is the
same WebView-login-then-in-page-fetch approach as everything else.

**Worth checking before building anything:** Cedarville may have PaperCut
configured to accept your normal AD credentials *and* may have an SSO option
behind a link I can't see unauthenticated.

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
