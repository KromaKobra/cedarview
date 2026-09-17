"""Chapel skips — against the real API.

**No longer a guess.** The earlier version of this module handled both JSON and
HTML with tolerant key matching, because nobody had seen the page. Now we have:
``scripts/discover chapel`` signed in, read the dashboard's own resource-timing
log, and found three JSON endpoints that are nowhere in the served HTML.

.. rubric:: The endpoints

``/cedarinfo/chapelskip`` 302s to ``/CedarInfo/chapelskip/StudentDashboard``,
a Vue 3 app which bootstraps itself with an inline ``const studentId = '…'``
and then calls::

    GET /CedarInfo/ChapelSkip/GetStudentSummaryJson?studentId=<id>
    GET /CedarInfo/ChapelSkip/GetStudentLedgerJson?studentId=<id>
    GET /CedarInfo/ChapelSkip/GetStudentFinesJson?studentId=<id>

All three need the Self-Service session, so they go through the WebView
transport. They are plain GETs — the page's ``__RequestVerificationToken`` is
for *removing* entries, not reading them, so no anti-forgery handling is needed
on this path.

Because the ID is a query parameter, the provider cannot skip straight to the
JSON: it fetches the dashboard first and reads the bootstrap out of it. One
extra request, once per refresh.

.. rubric:: Summary payload (verified 2026-09-17)

.. code-block:: json

    {"StudentId": "…", "StudentName": "…",
     "Term": "2026FA", "TermName": "Fall Semester 2026",
     "SkipsUsed": 2, "SkipsTotal": 18, "SkipsRemaining": 16,
     "AllowanceBreakdown": [
        {"Reason": "Skips Allowed",      "Count": 17, "Description": "Base semester allowance"},
        {"Reason": "Manual Arrangement", "Count": 1,  "Description": "For manual arrangement reasons"}],
     "RequirementReasons": ["Not a Distance Learner", "Registered for 15.5 credits (more than 6)",
                            "Undergraduate Student"],
     "IsRequiredToAttend": true, "IsInGoodStanding": true, "Status": "good"}

.. rubric:: Ledger payload — a ledger, not an attendance register

.. code-block:: json

    [{"Count": 1,  "ChapelDate": "2026-08-20T10:00:00", "EntryType": "Chapel Skip",
      "CreatedReason": "Absent from Chapel 8/20/2026", "CanRemove": false},
     {"Count": -1, "ChapelDate": null,                  "EntryType": "Manual Adjustment",
      "CreatedReason": "Had ID replaced", "CanRemove": true}]

``ChapelDate`` is ``null`` for adjustments — they are not tied to a chapel.

.. rubric:: The number you must not compute

The ledger above sums to ``1``. The server reports ``SkipsUsed: 2``. Both are
right on the server's own terms: the ``-1`` adjustment is *also* expressed as
the ``+1`` "Manual Arrangement" line in ``AllowanceBreakdown`` (17 + 1 = 18
total, 18 - 2 = 16 remaining). Recomputing from the ledger would show a
different, wrong number. **Use the reported figures.**
"""

from __future__ import annotations

import json
import logging
import re
from datetime import datetime
from typing import Any

from ..errors import ParseError
from ..models import AllowanceLine, ChapelLedgerEntry, ChapelSummary
from ..transport import Response, Transport
from .base import Provider

log = logging.getLogger(__name__)

#: The dashboard page. Requested only to read the student ID out of it.
CHAPEL_PATH = "/cedarinfo/chapelskip"

SUMMARY_PATH = "/CedarInfo/ChapelSkip/GetStudentSummaryJson"
LEDGER_PATH = "/CedarInfo/ChapelSkip/GetStudentLedgerJson"
FINES_PATH = "/CedarInfo/ChapelSkip/GetStudentFinesJson"

#: How the Vue app hands itself the student ID. The real page's line reads
#: ``            const studentId = '1234567'`` (ID shown scrubbed).
STUDENT_ID_RE = re.compile(r"""\bstudentId\s*=\s*['"](\d+)['"]""")


class ChapelProvider(Provider[ChapelSummary]):
    """Chapel skip balance and ledger for the signed-in student."""

    path = CHAPEL_PATH
    label = "Chapel"

    def __init__(self, transport: Transport) -> None:
        super().__init__(transport)
        #: Cached between refreshes — it does not change for a given login, and
        #: re-fetching a 59 KB page to re-read a constant would be wasteful.
        self._student_id: str = ""

    def fetch(self) -> ChapelSummary:
        """Read the dashboard for the ID, then the three JSON endpoints.

        Overridden rather than using the base single-request ``fetch`` because
        this provider genuinely needs four requests.
        """
        student_id = self.student_id()

        summary = self.transport.get(
            f"{SUMMARY_PATH}?studentId={student_id}"
        ).raise_for_session()
        ledger = self.transport.get(
            f"{LEDGER_PATH}?studentId={student_id}"
        ).raise_for_session()

        # Fines is the least important of the three and the most likely to be
        # added, renamed or restricted later. A failure here must not cost you
        # the skip count, which is the whole point of the screen.
        fines_data: Any = []
        try:
            fines_data = self.transport.get(
                f"{FINES_PATH}?studentId={student_id}"
            ).raise_for_session().json()
        except Exception as exc:  # noqa: BLE001
            log.warning("chapel fines unavailable (continuing): %r", exc)

        return build_summary(summary.json(), ledger.json(), fines_data)

    def student_id(self) -> str:
        """The signed-in student's ID, read from the dashboard's bootstrap."""
        if not self._student_id:
            page = self.transport.get(CHAPEL_PATH).raise_for_session()
            self._student_id = extract_student_id(page.body)
        return self._student_id

    def parse(self, response: Response) -> ChapelSummary:
        """Not used — :meth:`fetch` is overridden. Kept for the ABC."""
        raise NotImplementedError("ChapelProvider composes several requests; use fetch()")


def extract_student_id(html: str) -> str:
    """Pull ``const studentId = '1234567'`` out of the dashboard page."""
    match = STUDENT_ID_RE.search(html or "")
    if not match:
        raise ParseError(
            "could not find the studentId bootstrap in the chapel dashboard. "
            "The page is a Vue app that sets `const studentId = '…'` inline; if "
            "that changed, recapture with `scripts/discover chapel`."
        )
    return match.group(1)


def build_summary(summary: Any, ledger: Any, fines: Any = ()) -> ChapelSummary:
    """Combine the three payloads into one :class:`ChapelSummary`."""
    if not isinstance(summary, dict):
        raise ParseError(
            f"expected an object from {SUMMARY_PATH}, got {type(summary).__name__}"
        )

    return ChapelSummary(
        # Reported, never computed. See this module's docstring.
        used=_as_int(summary.get("SkipsUsed")),
        total=_as_int(summary.get("SkipsTotal")),
        remaining=_as_int(summary.get("SkipsRemaining")),
        term=_text(summary.get("Term")),
        term_name=_text(summary.get("TermName")),
        student_name=_text(summary.get("StudentName")),
        student_id=_text(summary.get("StudentId")),
        allowance=_parse_allowance(summary.get("AllowanceBreakdown")),
        requirement_reasons=tuple(
            _text(r) for r in (summary.get("RequirementReasons") or []) if _text(r)
        ),
        is_required_to_attend=bool(summary.get("IsRequiredToAttend", True)),
        is_in_good_standing=bool(summary.get("IsInGoodStanding", True)),
        status=_text(summary.get("Status")),
        entries=_parse_ledger(ledger),
        fines=tuple(f for f in (fines or []) if isinstance(f, dict)),
    )


def _parse_allowance(raw: Any) -> tuple[AllowanceLine, ...]:
    lines = []
    for item in raw or []:
        if not isinstance(item, dict):
            continue
        count = _as_int(item.get("Count"))
        if count is None:
            continue
        lines.append(
            AllowanceLine(
                reason=_text(item.get("Reason")),
                count=count,
                description=_text(item.get("Description")),
            )
        )
    return tuple(lines)


def _parse_ledger(raw: Any) -> tuple[ChapelLedgerEntry, ...]:
    """Parse the ledger, newest first.

    Sorted on ``CreatedAt`` rather than ``ChapelDate``, because adjustments have
    no chapel date at all and would otherwise have to be dropped or floated to
    one end arbitrarily.
    """
    if not isinstance(raw, list):
        raise ParseError(f"expected a list from {LEDGER_PATH}, got {type(raw).__name__}")

    entries = []
    for item in raw:
        if not isinstance(item, dict):
            continue
        entries.append(
            ChapelLedgerEntry(
                on=_parse_dt(item.get("ChapelDate")),
                count=_as_int(item.get("Count")) or 0,
                entry_type=_text(item.get("EntryType")),
                reason=_text(item.get("CreatedReason")),
                created_at=_parse_dt(item.get("CreatedAt")),
                can_remove=bool(item.get("CanRemove")),
            )
        )

    entries.sort(key=lambda e: (e.created_at is None, e.created_at or datetime.min), reverse=True)
    return tuple(entries)


def _parse_dt(value: Any) -> datetime | None:
    """Parse ``2026-08-20T10:00:00`` / ``…:29.623``; ``None`` stays ``None``.

    These come back without a zone. They are local Cedarville times and are
    treated as naive rather than being given a zone we would only be guessing
    at — nothing here does arithmetic across zones.
    """
    if not value:
        return None
    try:
        return datetime.fromisoformat(str(value))
    except ValueError:
        log.debug("chapel: unparseable timestamp %r", value)
        return None


def _as_int(value: Any) -> int | None:
    if value is None or isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        return int(value)
    match = re.search(r"-?\d+", str(value))
    return int(match.group()) if match else None


def _text(value: Any) -> str:
    return "" if value is None else " ".join(str(value).split())


def parse_body(body: str, *, content_type: str = "") -> ChapelSummary:
    """Back-compat shim for ``scripts/check-live``.

    Accepts a summary payload on its own and reports what it can, so the live
    canary keeps working without needing all three requests.
    """
    try:
        return build_summary(json.loads(body), [])
    except json.JSONDecodeError as exc:
        raise ParseError(f"chapel summary was not JSON: {body[:200]!r}") from exc
