// Chapel skips — against the real API.
//
// `scripts/discover chapel` signed in, read the dashboard's own resource-timing
// log, and found three JSON endpoints that are nowhere in the served HTML.
//
// The endpoints
// -------------
//
// `/cedarinfo/chapelskip` 302s to `/CedarInfo/chapelskip/StudentDashboard`, a
// Vue 3 app which bootstraps itself with an inline `const studentId = '…'` and
// then calls:
//
//     GET /CedarInfo/ChapelSkip/GetStudentSummaryJson?studentId=<id>
//     GET /CedarInfo/ChapelSkip/GetStudentLedgerJson?studentId=<id>
//     GET /CedarInfo/ChapelSkip/GetStudentFinesJson?studentId=<id>
//
// All three need the Self-Service session, so they go through the WebView
// transport. They are plain GETs — the page's `__RequestVerificationToken` is
// for *removing* entries, not reading them, so no anti-forgery handling is
// needed on this path.
//
// Because the ID is a query parameter, the JSON cannot be asked for until the
// ID is known. The first time, the provider fetches the dashboard and reads
// the bootstrap out of it. After that the ID is remembered (SessionState) and
// passed back in, so a refresh is the three JSON requests alone — and they are
// independent of each other, so they run at the same time. A remembered ID
// that has gone stale (ParseError, or a 4xx) is re-read off the dashboard and
// the three are asked once more.
//
// Summary payload (verified 2026-09-17)
// -------------------------------------
//
//     {"StudentId": "…", "StudentName": "…",
//      "Term": "2026FA", "TermName": "Fall Semester 2026",
//      "SkipsUsed": 2, "SkipsTotal": 18, "SkipsRemaining": 16,
//      "AllowanceBreakdown": [
//         {"Reason": "Skips Allowed",      "Count": 17, "Description": "Base semester allowance"},
//         {"Reason": "Manual Arrangement", "Count": 1,  "Description": "For manual arrangement reasons"}],
//      "RequirementReasons": ["Not a Distance Learner", "Registered for 15.5 credits (more than 6)",
//                             "Undergraduate Student"],
//      "IsRequiredToAttend": true, "IsInGoodStanding": true, "Status": "good"}
//
// Ledger payload — a ledger, not an attendance register
// -----------------------------------------------------
//
//     [{"Count": 1,  "ChapelDate": "2026-08-20T10:00:00", "EntryType": "Chapel Skip",
//       "CreatedReason": "Absent from Chapel 8/20/2026", "CanRemove": false},
//      {"Count": -1, "ChapelDate": null,                  "EntryType": "Manual Adjustment",
//       "CreatedReason": "Had ID replaced", "CanRemove": true}]
//
// `ChapelDate` is null for adjustments — they are not tied to a chapel.
//
// The number you must not compute
// -------------------------------
//
// The ledger above sums to 1. The server reports `SkipsUsed: 2`. Both are
// right on the server's own terms: the -1 adjustment is *also* expressed as the
// +1 "Manual Arrangement" line in `AllowanceBreakdown` (17 + 1 = 18 total,
// 18 - 2 = 16 remaining). Recomputing from the ledger would show a different,
// wrong number. **Use the reported figures.**

#pragma once

#include "../models.h"
#include "../transport.h"

#include <QJsonObject>
#include <QJsonValue>

namespace mycu {

// The dashboard page. Requested only to read the student ID out of it.
inline const QString CHAPEL_PATH = QStringLiteral("/cedarinfo/chapelskip");

inline const QString SUMMARY_PATH = QStringLiteral("/CedarInfo/ChapelSkip/GetStudentSummaryJson");
inline const QString LEDGER_PATH = QStringLiteral("/CedarInfo/ChapelSkip/GetStudentLedgerJson");
inline const QString FINES_PATH = QStringLiteral("/CedarInfo/ChapelSkip/GetStudentFinesJson");

// Chapel skip balance and ledger for the signed-in student.
//
// Keep one instance for the life of the screen: it remembers the student ID
// between fetches.
class ChapelProvider
{
public:
    static inline const QString path = CHAPEL_PATH;
    static inline const QString label = QStringLiteral("Chapel");

    // `studentId` is one remembered from an earlier run, or empty to read it
    // off the dashboard on first use.
    explicit ChapelProvider(TransportPtr transport, QString studentId = QString());

    // The three endpoints' JSON as they arrived, as one object —
    // `{"summary": {…}, "ledger": […], "fines": […]}` — which is what the
    // on-device cache keeps. Every response is checked for an expired session
    // first.
    QJsonObject fetchPayload();

    // fetchPayload(), parsed.
    ChapelSummary fetch();

    // The signed-in student's ID: the one given or last read, else read from
    // the dashboard's bootstrap now.
    QString studentId();

    // The ID as it stands, without fetching anything — empty until known.
    QString knownStudentId() const { return m_studentId; }

private:
    QJsonObject fetchFor(const QString &id) const;

    TransportPtr m_transport;
    // Kept between fetches — it does not change for a given login, and
    // re-fetching a 59 KB page to re-read a constant would be wasteful.
    QString m_studentId;
};

// Pull `const studentId = '1234567'` out of the dashboard page.
QString extractStudentId(const QString &html);

// Combine the three payloads into one ChapelSummary.
ChapelSummary buildSummary(const QJsonValue &summary, const QJsonValue &ledger,
                           const QJsonValue &fines = QJsonValue());

// The same, from fetchPayload()'s single object — a fresh one or a cached one.
ChapelSummary buildSummary(const QJsonObject &payload);

// For `scripts/check-live`-style canaries: a summary payload on its own,
// reporting what it can without the ledger.
ChapelSummary parseBody(const QString &body);

} // namespace mycu
