"""Chapel attendance — the first provider.

    GET https://selfservice.cedarville.edu/CedarInfo/ChapelAttendance
      -> 301/302 https://selfservice.cedarville.edu/cedarinfo/chapelskip

(Both hops verified live. Unauthenticated, the canonical path 302s to
``login.microsoftonline.com/81c32413-.../saml2`` with
``RelayState=%2Fcedarinfo%2Fchapelskip``.)

.. rubric:: What is confirmed and what is not

**Confirmed:** the host, the path, the redirect, and that it is a SAML SP
against Entra ID.

**Not confirmed:** whether ``/cedarinfo/chapelskip`` serves JSON or a
server-rendered Razor page, and — if JSON — what its keys are called. That
answer needs an authenticated session, which only you can produce
(``docs/discovery.md``, milestone M0). ``/cedarinfo/`` is a Cedarville-custom
module, so the JSON-heavy behaviour of Ellucian's stock ``/Student/`` areas is
not evidence either way.

This module therefore handles **both**, chosen at runtime by sniffing the
response:

* JSON -> :func:`parse_json`, with tolerant key matching over a list of
  plausible spellings.
* HTML -> :func:`parse_html`, which locates the attendance table by its header
  text rather than by position or CSS class.

When M0 tells you which it is, delete the branch you do not need and replace the
candidate-key lists with the real names. Everything marked ``# M0:`` is a guess
awaiting that confirmation::

    grep -rn 'M0:' mycu/
"""

from __future__ import annotations

import json
import logging
import re
from datetime import date, datetime
from typing import Any, Iterable, Sequence

from ..errors import ParseError
from ..models import AttendanceStatus, ChapelRecord, ChapelSummary
from ..transport import Response
from .base import Provider

log = logging.getLogger(__name__)

#: Canonical path. The ``/CedarInfo/ChapelAttendance`` spelling redirects here,
#: so we request the destination directly and save a round trip.
CHAPEL_PATH = "/cedarinfo/chapelskip"

# --------------------------------------------------------------------------
# Candidate field names.
#
# M0: every list below is a guess. Each is ordered most-likely-first and
# matched case-insensitively with non-alphanumerics stripped, so "ChapelDate",
# "chapel_date" and "Chapel Date" all collapse to the same key.
# --------------------------------------------------------------------------

DATE_KEYS = ("date", "chapeldate", "eventdate", "attendancedate", "meetingdate", "day")
STATUS_KEYS = ("status", "attendancestatus", "attendance", "code", "statuscode", "type")
TITLE_KEYS = ("title", "speaker", "event", "description", "eventtitle", "topic")
NOTE_KEYS = ("note", "notes", "comment", "comments", "reason", "excusereason")

RECORD_LIST_KEYS = (
    "records", "attendance", "attendancerecords", "chapelrecords",
    "items", "data", "rows", "results", "events", "sessions", "list",
)
#: Note what is *absent* from these: bare "skips", bare "name", bare "date".
#: They are too generic to survive the substring fallback in :func:`_pick` —
#: "skips" matches "allowedSkips" and "skipsRemaining", and "name" matches
#: "termName". A wrong-but-plausible number on the summary line is worse than no
#: number, so the generic spellings are deliberately not searched for.
ALLOWED_KEYS = ("skipsallowed", "allowedskips", "maxskips", "allotted", "allowance", "allowed")
USED_KEYS = ("skipsused", "usedskips", "chapelskipsused", "unexcusedabsences", "absences", "used")
REMAINING_KEYS = ("skipsremaining", "remainingskips", "remaining")
TERM_KEYS = ("termname", "termdescription", "semester", "termid", "term")
NAME_KEYS = ("studentname", "displayname", "fullname", "name")

#: Header texts that identify each column of a server-rendered table. Matched
#: as substrings of the normalised header cell.
HTML_DATE_HEADERS = ("date", "day")
HTML_STATUS_HEADERS = ("status", "attendance", "present", "absent", "code")
HTML_TITLE_HEADERS = ("speaker", "title", "event", "topic", "description")
HTML_NOTE_HEADERS = ("note", "comment", "reason")

#: Date formats to try, in order. The first four cover essentially everything
#: ASP.NET and Razor emit for a US institution.
DATE_FORMATS = (
    "%m/%d/%Y",
    "%m/%d/%y",
    "%Y-%m-%d",
    "%b %d, %Y",
    "%B %d, %Y",
    "%d %b %Y",
    "%m-%d-%Y",
)


def _norm(text: str) -> str:
    """Collapse a key or header to bare lowercase alphanumerics.

    ``"Chapel Date"``, ``"chapel_date"`` and ``"ChapelDate"`` all become
    ``"chapeldate"``, which is what makes the candidate lists above tolerant of
    whichever casing convention Cedarville happened to use.
    """
    return re.sub(r"[^a-z0-9]+", "", (text or "").lower())


def _pick(mapping: dict[str, Any], candidates: Sequence[str], *, fuzzy: bool = True) -> Any:
    """First value in ``mapping`` whose normalised key matches a candidate.

    Exact normalised matches are always preferred, so a payload containing both
    ``"date"`` and ``"dateCreated"`` resolves the way you want. Pass
    ``fuzzy=False`` to disable the substring fallback entirely — which is what
    :func:`_find_scalar` does on its first pass over a nested payload, so that a
    precise match deep in the tree beats a sloppy match near the top.
    """
    normalised = {_norm(k): v for k, v in mapping.items()}

    for candidate in candidates:
        if candidate in normalised and normalised[candidate] not in (None, ""):
            return normalised[candidate]

    if not fuzzy:
        return None

    for candidate in candidates:
        for key, value in normalised.items():
            if candidate in key and value not in (None, ""):
                return value

    return None


def _as_int(value: Any) -> int | None:
    """Coerce ``6``, ``"6"``, ``"6 skips"`` or ``"6.0"`` to ``6``; else ``None``."""
    if value is None or isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        return int(value)
    match = re.search(r"-?\d+", str(value))
    return int(match.group()) if match else None


def parse_date(raw: Any) -> date | None:
    """Parse a date from whatever the payload happened to contain.

    Handles the three shapes that actually turn up in ASP.NET output:

    * ISO 8601 with or without a time component (``2026-09-16T00:00:00``);
    * US-style display strings (``9/16/2026``, ``Sep 16, 2026``);
    * the legacy Microsoft JSON epoch (``"/Date(1758000000000)/"``).

    Returns ``None`` rather than raising — an unparseable date degrades one row
    to showing its original text, which is better than losing the whole page.
    """
    if raw is None:
        return None
    if isinstance(raw, date) and not isinstance(raw, datetime):
        return raw
    if isinstance(raw, datetime):
        return raw.date()

    text = str(raw).strip()
    if not text:
        return None

    epoch = re.fullmatch(r"/Date\((-?\d+)(?:[+-]\d{4})?\)/", text)
    if epoch:
        return datetime.fromtimestamp(int(epoch.group(1)) / 1000).date()

    try:
        return datetime.fromisoformat(text.replace("Z", "+00:00")).date()
    except ValueError:
        pass

    for fmt in DATE_FORMATS:
        try:
            return datetime.strptime(text, fmt).date()
        except ValueError:
            continue

    # Last resort: a date embedded in a longer string ("Tue 9/16/2026").
    embedded = re.search(r"\d{1,4}[/-]\d{1,2}[/-]\d{2,4}", text)
    if embedded:
        return parse_date(embedded.group())

    log.debug("unparseable date %r", text)
    return None


# --------------------------------------------------------------------------
# JSON branch
# --------------------------------------------------------------------------

def _find_record_list(data: Any) -> list[dict[str, Any]]:
    """Locate the list of per-session records inside an arbitrary JSON payload.

    Tried in order:

    1. The payload *is* a list of objects.
    2. A top-level key from :data:`RECORD_LIST_KEYS` holds one.
    3. Any nested list of objects that looks like attendance rows — i.e. whose
       first element has both a date-ish and a status-ish key.

    Step 3 exists because ASP.NET habitually wraps payloads
    (``{"d": {"Model": {"Records": [...]}}}``) and hunting for the wrapper by
    hand is not worth it when the row shape is this recognisable.
    """
    if isinstance(data, list) and all(isinstance(x, dict) for x in data):
        return data

    if isinstance(data, dict):
        for key in RECORD_LIST_KEYS:
            for actual, value in data.items():
                if _norm(actual) == key and isinstance(value, list):
                    if all(isinstance(x, dict) for x in value):
                        return value

        best: list[dict[str, Any]] = []
        for value in _walk(data):
            if isinstance(value, list) and value and all(isinstance(x, dict) for x in value):
                head = value[0]
                if _pick(head, DATE_KEYS) is not None and _pick(head, STATUS_KEYS) is not None:
                    if len(value) > len(best):
                        best = value
        if best:
            log.info("chapel: found %d records in a nested list", len(best))
            return best

    return []


def _walk(node: Any) -> Iterable[Any]:
    """Yield every value in a nested JSON structure, depth first."""
    yield node
    if isinstance(node, dict):
        for value in node.values():
            yield from _walk(value)
    elif isinstance(node, list):
        for value in node:
            yield from _walk(value)


def _find_scalar(data: Any, candidates: Sequence[str]) -> Any:
    """Search the whole payload for a scalar under one of ``candidates``.

    Two passes over the tree: exact key matches first, everywhere, and only then
    substring matches. Without that ordering an approximate hit in a shallow
    wrapper object would beat the exact hit that is one level deeper — which for
    a payload like ``{"model": {"student": {"displayName": …}}}`` is the
    difference between reading the student's name and reading the term's.
    """
    for fuzzy in (False, True):
        for node in _walk(data):
            if isinstance(node, dict):
                value = _pick(node, candidates, fuzzy=fuzzy)
                if value is not None and not isinstance(value, (dict, list)):
                    return value
    return None


def parse_json(data: Any) -> ChapelSummary:
    """Build a :class:`ChapelSummary` from a decoded JSON payload."""
    rows = _find_record_list(data)

    records = []
    for row in rows:
        raw_date = _pick(row, DATE_KEYS)
        records.append(
            ChapelRecord(
                on=parse_date(raw_date),
                status=AttendanceStatus.parse(_stringify(_pick(row, STATUS_KEYS))),
                raw_date="" if raw_date is None else str(raw_date),
                title=_stringify(_pick(row, TITLE_KEYS)) or "",
                note=_stringify(_pick(row, NOTE_KEYS)) or "",
            )
        )

    allowed = _as_int(_find_scalar(data, ALLOWED_KEYS))
    used = _as_int(_find_scalar(data, USED_KEYS))
    remaining = _as_int(_find_scalar(data, REMAINING_KEYS))

    # Prefer Cedarville's own arithmetic over ours: if it reports two of the
    # three numbers, derive the third rather than recounting rows.
    if used is None and allowed is not None and remaining is not None:
        used = allowed - remaining
    if used is None:
        used = sum(1 for r in records if r.counts_as_skip)

    return ChapelSummary(
        records=tuple(records),
        allowed=allowed,
        used=used,
        term=_stringify(_find_scalar(data, TERM_KEYS)) or "",
        student_name=_stringify(_find_scalar(data, NAME_KEYS)) or "",
    )


def _stringify(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, str):
        return value.strip()
    return str(value)


# --------------------------------------------------------------------------
# HTML branch
# --------------------------------------------------------------------------

def parse_html(body: str) -> ChapelSummary:
    """Build a :class:`ChapelSummary` from a server-rendered page.

    Columns are found by reading the ``<th>`` text, never by index, so a column
    inserted upstream does not silently shift every field by one. If no table
    with a recognisable date column exists, this raises :class:`ParseError` —
    which is the correct outcome, because silently returning zero records would
    read as "you have no absences".
    """
    try:
        from lxml import html as lxml_html
    except ImportError as exc:  # pragma: no cover - dependency is in the flake
        raise ParseError(
            "lxml is required to parse the HTML form of the chapel page; "
            "it is in flake.nix's pythonEnv"
        ) from exc

    tree = lxml_html.fromstring(body)

    for table in tree.xpath("//table"):
        columns = _header_columns(table)
        if "date" not in columns:
            continue

        records = []
        for row in table.xpath(".//tbody/tr | .//tr[not(th)]"):
            cells = [_text(c) for c in row.xpath("./td")]
            if not cells:
                continue

            raw_date = _cell(cells, columns.get("date"))
            status_text = _cell(cells, columns.get("status"))
            records.append(
                ChapelRecord(
                    on=parse_date(raw_date),
                    status=AttendanceStatus.parse(status_text),
                    raw_date=raw_date,
                    title=_cell(cells, columns.get("title")),
                    note=_cell(cells, columns.get("note")),
                )
            )

        if records:
            page_text = _text(tree)
            return ChapelSummary(
                records=tuple(records),
                allowed=_scan_number(page_text, ("allowed", "allotted", "allowance", "permitted")),
                used=(
                    _scan_number(page_text, ("used", "taken", "recorded"))
                    or sum(1 for r in records if r.counts_as_skip)
                ),
                term=_scan_term(page_text),
                student_name="",  # M0: only fill this in if the page shows it.
            )

    raise ParseError(
        "no chapel attendance table found in the HTML response — the page "
        "layout has probably changed; recapture it per docs/discovery.md"
    )


def _header_columns(table: Any) -> dict[str, int]:
    """Map our column roles to indices, using the table's header text."""
    headers = [_norm(_text(th)) for th in table.xpath(".//th")]
    if not headers:
        first = table.xpath(".//tr[1]/td")
        headers = [_norm(_text(td)) for td in first]

    roles = {
        "date": HTML_DATE_HEADERS,
        "status": HTML_STATUS_HEADERS,
        "title": HTML_TITLE_HEADERS,
        "note": HTML_NOTE_HEADERS,
    }

    found: dict[str, int] = {}
    for role, needles in roles.items():
        for index, header in enumerate(headers):
            if any(needle in header for needle in needles) and index not in found.values():
                found[role] = index
                break
    return found


def _text(node: Any) -> str:
    """Visible text of an element, whitespace collapsed."""
    return re.sub(r"\s+", " ", node.text_content()).strip()


def _cell(cells: list[str], index: int | None) -> str:
    if index is None or index >= len(cells):
        return ""
    return cells[index]


def _scan_number(text: str, needles: Sequence[str]) -> int | None:
    """Find "you have used 3 of 6" style numbers near a keyword.

    Looks *after* the keyword first and only then before it, and when looking
    before takes the nearest number rather than the first. Direction matters a
    great deal here: in

        "You are allowed 6 chapel skips this semester. You have used 3 skips."

    a symmetric window around "used" contains the 6 from the previous sentence
    before it contains the 3 — and reporting "6 of 6 skips used" to someone who
    has used 3 is exactly the kind of wrong that would matter.
    """
    for needle in needles:
        for match in re.finditer(rf"\b{re.escape(needle)}\b", text, re.I):
            after = re.search(r"\b(\d{1,3})\b", text[match.end(): match.end() + 40])
            if after:
                return int(after.group(1))

            before = re.findall(r"\b(\d{1,3})\b", text[max(0, match.start() - 40): match.start()])
            if before:
                return int(before[-1])
    return None


def _scan_term(text: str) -> str:
    """Pull a term label like "Fall 2026" out of the page text."""
    match = re.search(r"\b(Fall|Spring|Summer|Winter)\s+(\d{4})\b", text, re.I)
    return f"{match.group(1).title()} {match.group(2)}" if match else ""


# --------------------------------------------------------------------------
# The provider
# --------------------------------------------------------------------------

class ChapelProvider(Provider[ChapelSummary]):
    """Chapel attendance for the logged-in student."""

    path = CHAPEL_PATH
    label = "Chapel"

    def parse(self, response: Response) -> ChapelSummary:
        return parse_body(response.body, content_type=response.headers.get("content-type", ""))


def parse_body(body: str, *, content_type: str = "") -> ChapelSummary:
    """Dispatch to the JSON or HTML parser by sniffing the payload.

    The ``Content-Type`` header is used when present but is not trusted alone —
    the in-page ``fetch()`` transport does not always surface headers, and
    ASP.NET has been known to serve JSON as ``text/html``. So the body itself
    gets the final say: if it parses as JSON, it is JSON.
    """
    stripped = (body or "").lstrip()
    if not stripped:
        raise ParseError("empty response body for the chapel page")

    looks_json = stripped[0] in "{[" or "json" in content_type.lower()
    if looks_json:
        try:
            return parse_json(json.loads(stripped))
        except json.JSONDecodeError:
            if "json" in content_type.lower():
                raise ParseError(
                    f"Content-Type claimed JSON but the body did not parse: "
                    f"{stripped[:200]!r}"
                ) from None
            # Fall through: a body starting with '{' can still be a page.

    return parse_html(body)
