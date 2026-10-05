#include "chapel.h"

#include "../log.h"
#include "../pyjson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <future>

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

ChapelProvider::ChapelProvider(TransportPtr transport, QString studentId)
    : m_transport(std::move(transport))
    , m_studentId(std::move(studentId))
{}

QJsonObject ChapelProvider::fetchFor(const QString &id) const
{
    const QString query = QStringLiteral("?studentId=") + id;
    auto json = [transport = m_transport](const QString &path) {
        return transport->get(path).raiseForSession().json();
    };

    // All three at once. They are independent, and every transport here can
    // carry concurrent requests: WebViewTransport keys each one by its own
    // token, and the Android and desktop HTTPS paths are one connection per
    // call. std::async rather than the thread pool, because the pool is where
    // this function is already running — a worker that blocked waiting on
    // tasks queued behind it could starve itself.
    auto summary = std::async(std::launch::async, json, SUMMARY_PATH + query);
    auto ledger = std::async(std::launch::async, json, LEDGER_PATH + query);
    // Fines is the least important of the three and the most likely to be
    // added, renamed or restricted later. A failure here must not cost you the
    // skip count, which is the whole point of the screen.
    auto fines = std::async(std::launch::async, [json, path = FINES_PATH + query]() -> QJsonValue {
        try {
            return json(path);
        } catch (const std::exception &e) {
            qCWarning(lcChapel) << "chapel fines unavailable (continuing):" << e.what();
            return QJsonArray();
        }
    });

    // get() rethrows a worker's exception here; a future that is never got
    // still waits for its request in its destructor, so nothing outlives this.
    QJsonObject payload;
    payload.insert(QStringLiteral("summary"), summary.get());
    payload.insert(QStringLiteral("ledger"), ledger.get());
    payload.insert(QStringLiteral("fines"), fines.get());
    return payload;
}

QJsonObject ChapelProvider::fetchPayload()
{
    const QString remembered = m_studentId;
    if (remembered.isEmpty()) {
        qCDebug(lcChapel) << "chapel: no student ID yet; 4 requests (the dashboard, then 3 at once)";
        return fetchFor(studentId());
    }
    qCDebug(lcChapel) << "chapel: student ID known; 3 requests at once";

    std::exception_ptr failure;
    try {
        QJsonObject payload = fetchFor(remembered);
        // A wrong ID shows up as a summary in the wrong shape as often as an
        // error, so the parse is the check.
        buildSummary(payload);
        return payload;
    } catch (const ParseError &e) {
        qCInfo(lcChapel) << "chapel: the remembered student ID failed (" << e.what() << "); re-reading it";
        failure = std::current_exception();
    } catch (const TransportError &e) {
        if (!e.isClientError())
            throw;
        qCInfo(lcChapel) << "chapel: the remembered student ID was refused (" << e.what() << "); re-reading it";
        failure = std::current_exception();
    }

    // Once, and only if the dashboard names someone else: when the ID was
    // right all along, asking again would fail the same way.
    m_studentId.clear();
    if (studentId() == remembered)
        std::rethrow_exception(failure);
    qCDebug(lcChapel) << "chapel: the dashboard names another student; 3 requests more";
    return fetchFor(m_studentId);
}

ChapelSummary ChapelProvider::fetch()
{
    return buildSummary(fetchPayload());
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

ChapelSummary buildSummary(const QJsonObject &payload)
{
    return buildSummary(payload.value(QStringLiteral("summary")), payload.value(QStringLiteral("ledger")),
                        payload.value(QStringLiteral("fines")));
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
