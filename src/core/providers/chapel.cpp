#include "chapel.h"

#include "../log.h"
#include "../pyjson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace mycu {

namespace {

// How the Vue app hands itself the student ID. The real page's line reads
// `            const studentId = '1234567'` (ID shown scrubbed).
const QRegularExpression &studentIdPattern()
{
    static const QRegularExpression re(QStringLiteral(R"(\bstudentId\s*=\s*['"](\d+)['"])"));
    return re;
}

QList<AllowanceLine> parseAllowance(const QJsonValue &raw)
{
    QList<AllowanceLine> lines;
    for (const QJsonValue &item : raw.toArray()) {
        if (!item.isObject())
            continue;
        const QJsonObject obj = item.toObject();
        const std::optional<int> count = json::asInt(obj.value(QStringLiteral("Count")));
        if (!count)
            continue;
        lines.append({json::clean(obj.value(QStringLiteral("Reason"))), *count,
                      json::clean(obj.value(QStringLiteral("Description")))});
    }
    return lines;
}

// A ChapelDate / CreatedAt value. These come back without a zone. They are
// local Cedarville times and are read as local rather than being given a zone
// we would only be guessing at — nothing here does arithmetic across zones.
QDateTime parseDt(const QJsonValue &value)
{
    const QDateTime parsed = json::parseIsoDateTime(value);
    if (!parsed.isValid() && json::truthy(value))
        qCDebug(lcChapel) << "chapel: unparseable timestamp" << json::repr(value);
    return parsed;
}

// Parse the ledger, newest first.
//
// Sorted on CreatedAt rather than ChapelDate, because adjustments have no
// chapel date at all and would otherwise have to be dropped or floated to one
// end arbitrarily. Entries with no CreatedAt come first, as they always have.
QList<ChapelLedgerEntry> parseLedger(const QJsonValue &raw)
{
    if (!raw.isArray()) {
        throw ParseError(QStringLiteral("expected a list from %1, got %2")
                             .arg(LEDGER_PATH, json::typeName(raw)));
    }

    QList<ChapelLedgerEntry> entries;
    for (const QJsonValue &item : raw.toArray()) {
        if (!item.isObject())
            continue;
        const QJsonObject obj = item.toObject();
        ChapelLedgerEntry entry;
        entry.on = parseDt(obj.value(QStringLiteral("ChapelDate")));
        entry.count = json::asInt(obj.value(QStringLiteral("Count"))).value_or(0);
        entry.entryType = json::clean(obj.value(QStringLiteral("EntryType")));
        entry.reason = json::clean(obj.value(QStringLiteral("CreatedReason")));
        entry.createdAt = parseDt(obj.value(QStringLiteral("CreatedAt")));
        entry.canRemove = json::truthy(obj.value(QStringLiteral("CanRemove")));
        entries.append(entry);
    }

    // Newest first, undated first of all; stable, so equal keys keep the
    // order they arrived in.
    std::stable_sort(entries.begin(), entries.end(),
                     [](const ChapelLedgerEntry &a, const ChapelLedgerEntry &b) {
                         const bool aNone = !a.createdAt.isValid();
                         const bool bNone = !b.createdAt.isValid();
                         if (aNone != bNone)
                             return aNone;
                         if (aNone)
                             return false;
                         return a.createdAt > b.createdAt;
                     });
    return entries;
}

} // namespace

ChapelProvider::ChapelProvider(TransportPtr transport)
    : m_transport(std::move(transport))
{}

ChapelSummary ChapelProvider::fetch()
{
    const QString id = studentId();

    const Response summary =
        m_transport->get(SUMMARY_PATH + QStringLiteral("?studentId=") + id);
    summary.raiseForSession();
    const Response ledger = m_transport->get(LEDGER_PATH + QStringLiteral("?studentId=") + id);
    ledger.raiseForSession();

    // Fines is the least important of the three and the most likely to be
    // added, renamed or restricted later. A failure here must not cost you the
    // skip count, which is the whole point of the screen.
    QJsonValue fines = QJsonArray();
    try {
        const Response response = m_transport->get(FINES_PATH + QStringLiteral("?studentId=") + id);
        fines = response.raiseForSession().json();
    } catch (const std::exception &e) {
        qCWarning(lcChapel) << "chapel fines unavailable (continuing):" << e.what();
    }

    return buildSummary(summary.json(), ledger.json(), fines);
}

QString ChapelProvider::studentId()
{
    if (m_studentId.isEmpty()) {
        const Response page = m_transport->get(CHAPEL_PATH);
        page.raiseForSession();
        m_studentId = extractStudentId(page.body);
    }
    return m_studentId;
}

QString extractStudentId(const QString &html)
{
    const QRegularExpressionMatch match = studentIdPattern().match(html);
    if (!match.hasMatch()) {
        throw ParseError(QStringLiteral(
            "could not find the studentId bootstrap in the chapel dashboard. The page is a Vue app "
            "that sets `const studentId = '…'` inline; if that changed, recapture with "
            "`scripts/discover chapel`."));
    }
    return match.captured(1);
}

ChapelSummary buildSummary(const QJsonValue &summaryValue, const QJsonValue &ledger,
                           const QJsonValue &fines)
{
    if (!summaryValue.isObject()) {
        throw ParseError(QStringLiteral("expected an object from %1, got %2")
                             .arg(SUMMARY_PATH, json::typeName(summaryValue)));
    }
    const QJsonObject summary = summaryValue.toObject();

    ChapelSummary out;
    // Reported, never computed. See this file's header.
    out.used = json::asInt(summary.value(QStringLiteral("SkipsUsed")));
    out.total = json::asInt(summary.value(QStringLiteral("SkipsTotal")));
    out.remaining = json::asInt(summary.value(QStringLiteral("SkipsRemaining")));
    out.term = json::clean(summary.value(QStringLiteral("Term")));
    out.termName = json::clean(summary.value(QStringLiteral("TermName")));
    out.studentName = json::clean(summary.value(QStringLiteral("StudentName")));
    out.studentId = json::clean(summary.value(QStringLiteral("StudentId")));
    out.allowance = parseAllowance(summary.value(QStringLiteral("AllowanceBreakdown")));
    for (const QJsonValue &reason : summary.value(QStringLiteral("RequirementReasons")).toArray()) {
        const QString text = json::clean(reason);
        if (!text.isEmpty())
            out.requirementReasons.append(text);
    }
    out.isRequiredToAttend = json::truthy(summary.value(QStringLiteral("IsRequiredToAttend")), true);
    out.isInGoodStanding = json::truthy(summary.value(QStringLiteral("IsInGoodStanding")), true);
    out.status = json::clean(summary.value(QStringLiteral("Status")));
    out.entries = parseLedger(ledger);
    for (const QJsonValue &fine : fines.toArray()) {
        if (fine.isObject())
            out.fines.append(fine.toObject());
    }
    return out;
}

ChapelSummary parseBody(const QString &body)
{
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError)
        throw ParseError(QStringLiteral("chapel summary was not JSON: '%1'").arg(body.left(200)));
    return buildSummary(doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object()),
                        QJsonArray());
}

} // namespace mycu
